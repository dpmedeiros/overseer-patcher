"""Installer integration tests using a synthetic Steam installation."""

import importlib.machinery
import importlib.util
import io
import struct
import sys
import tarfile
import tempfile
import unittest
from contextlib import redirect_stderr
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).with_name("overseer-patch")
loader = importlib.machinery.SourceFileLoader("overseer_patch", str(SCRIPT))
spec = importlib.util.spec_from_loader(loader.name, loader)
patcher = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = patcher
loader.exec_module(patcher)


def fake_dll(marker: int) -> bytes:
    data = bytearray(128)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 64)
    data[64:68] = b"PE\0\0"
    struct.pack_into("<H", data, 68, 0x14C)
    data[-1] = marker
    return bytes(data)


class PatchFixture(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="overseer-patcher-test-")
        self.addCleanup(self.temp.cleanup)
        self.steam = Path(self.temp.name) / "Steam"
        apps = self.steam / "steamapps"
        self.game = apps / "common/Overseer"
        prefix = apps / "compatdata/302370/pfx"
        tool = self.steam / "compatibilitytools.d/Proton-CachyOS Latest-x86_64_v3"
        self.game.mkdir(parents=True)
        prefix.mkdir(parents=True)
        tool.mkdir(parents=True)
        (apps / "appmanifest_302370.acf").write_text('"AppState"\n{\n "installdir" "Overseer"\n}\n')
        (self.steam / "config").mkdir()
        self.config = self.steam / "config/config.vdf"
        self.original_config = ('"InstallConfigStore"\n{\n "CompatToolMapping"\n {\n'
                                '  "0"\n  {\n   "name" "other"\n  }\n }\n}\n')
        self.config.write_text(self.original_config)
        self.registry = prefix / "user.reg"
        self.original_registry = 'WINE REGISTRY Version 2\n\n[Software\\\\Other] 123\n"Example"="keep"\n'
        self.registry.write_text(self.original_registry)
        (tool / "proton").write_text("#!/bin/sh\n")
        (tool / "version").write_text(patcher.RELEASE_TAG)
        (tool / "compatibilitytool.vdf").write_text('"compatibilitytools" { "compat_tools" { "Proton-CachyOS Latest" {} } }')
        (self.game / "OVERSEER.EXE").write_bytes(b"synthetic exe")
        self.bundled_dll = fake_dll(1)
        self.patches = [
            mock.patch.object(patcher, "ORIGINAL_EXE_SHA", patcher.sha(b"synthetic exe")),
            mock.patch.object(patcher, "wrapper_bytes", return_value=self.bundled_dll),
            mock.patch.object(patcher, "steam_running", return_value=False),
        ]
        for item in self.patches:
            item.start()
            self.addCleanup(item.stop)
        self.ctx = patcher.discover(self.steam)

    def test_apply_and_revert_without_game_data(self):
        self.assertEqual(patcher.verify_context(self.ctx), (None, None, None, None))
        patcher.apply(self.ctx)
        self.assertFalse((self.game / "DATA").exists())
        self.assertEqual(self.ctx.dll_file.read_bytes(), self.bundled_dll)
        self.assertEqual(patcher.verify_context(self.ctx),
                         ("128", patcher.TOOL_ID,
                          patcher.DLL_OVERRIDE, patcher.sha(self.bundled_dll)))
        self.assertTrue(patcher.alias_path(self.steam).is_dir())
        patcher.apply(self.ctx)  # Idempotent with its own journal.
        patcher.revert(self.ctx)
        self.assertFalse((self.game / "DATA").exists())
        self.assertFalse(self.ctx.dll_file.exists())
        self.assertEqual(self.registry.read_text(), self.original_registry)
        self.assertEqual(self.config.read_text(), self.original_config)
        self.assertFalse(patcher.alias_path(self.steam).exists())
        self.assertIsNone(patcher.read_journal(self.ctx))

    def test_revert_restores_preexisting_dll_and_override(self):
        prior = fake_dll(2)
        self.ctx.dll_file.write_bytes(prior)
        self.registry.write_text(patcher.edit_registry(
            self.original_registry, "builtin", patcher.DLL_SECTION, patcher.DLL_KEY))
        before = self.registry.read_bytes()
        patcher.apply(self.ctx)
        self.assertEqual(self.ctx.dll_file.read_bytes(), self.bundled_dll)
        patcher.revert(self.ctx)
        self.assertEqual(self.ctx.dll_file.read_bytes(), prior)
        self.assertEqual(self.registry.read_bytes(), before)

    def test_ignores_old_journal(self):
        old_journal = self.ctx.state.parent / "journal.json"
        old_journal.parent.mkdir(parents=True)
        old_journal.write_text('{"schema": 1}\n')
        patcher.apply(self.ctx)
        patcher.revert(self.ctx)
        self.assertEqual(old_journal.read_text(), '{"schema": 1}\n')

    def test_revert_rejects_modified_installed_dll(self):
        patcher.apply(self.ctx)
        self.ctx.dll_file.write_bytes(fake_dll(3))
        with self.assertRaisesRegex(patcher.PatchError, "ddraw.dll changed"):
            patcher.revert(self.ctx)
        self.assertTrue((self.ctx.state / "journal.json").exists())

    def test_revert_recovers_interrupted_apply(self):
        with mock.patch.object(patcher, "edit_mapping", side_effect=RuntimeError("simulated interruption")):
            with self.assertRaisesRegex(RuntimeError, "simulated interruption"):
                patcher.apply(self.ctx)
        self.assertEqual(patcher.read_journal(self.ctx)["state"], "prepared")
        patcher.revert(self.ctx)
        self.assertFalse(self.ctx.dll_file.exists())
        self.assertEqual(self.registry.read_text(), self.original_registry)
        self.assertEqual(self.config.read_text(), self.original_config)

    def test_missing_proton_downloads_verified_archive(self):
        payload = Path(self.temp.name) / "payload/Proton-CachyOS"
        payload.mkdir(parents=True)
        (payload / "proton").write_text("#!/bin/sh\n")
        (payload / "version").write_text(patcher.RELEASE_TAG)
        (payload / "compatibilitytool.vdf").write_text('"compatibilitytools" {}')
        archive = Path(self.temp.name) / "release.tar.xz"
        with tarfile.open(archive, "w:xz") as bundle:
            bundle.add(payload, arcname=payload.name)
        digest = "sha256:" + patcher.file_sha(archive)
        download_steam = Path(self.temp.name) / "FreshSteam"
        with mock.patch.object(patcher, "choose_proton", side_effect=patcher.PatchError("missing")):
            with mock.patch.object(patcher, "release_assets", return_value=(archive.as_uri(), digest, None)):
                installed = patcher.install_proton(download_steam)
        self.assertEqual((installed / "version").read_text(), patcher.RELEASE_TAG)
        self.assertTrue((installed / "proton").is_file())

    def test_guided_cli_confirms_between_stages(self):
        events = mock.Mock()
        with (mock.patch.object(sys, "argv", ["overseer-patch"]),
              mock.patch.object(patcher, "discover", return_value=self.ctx),
              mock.patch.object(patcher, "status") as status,
              mock.patch.object(patcher, "plan") as plan,
              mock.patch.object(patcher, "apply") as apply,
              mock.patch.object(patcher, "confirm_step", side_effect=[True, True]) as confirm):
            for name, patched in (("status", status), ("plan", plan),
                                  ("apply", apply), ("confirm", confirm)):
                events.attach_mock(patched, name)
            self.assertEqual(patcher.main(), 0)
        self.assertEqual(events.mock_calls, [
            mock.call.status(self.ctx),
            mock.call.confirm("Continue to proposed changes?"),
            mock.call.plan(self.ctx),
            mock.call.confirm("Apply these changes? Close Steam and Overseer first."),
            mock.call.apply(self.ctx),
        ])

    def test_cli_rejects_individual_phases(self):
        with mock.patch.object(sys, "argv", ["overseer-patch", "status"]):
            with redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    patcher.main()
        self.assertEqual(error.exception.code, 2)

    def test_guided_cli_stops_after_declining_status(self):
        with (mock.patch.object(sys, "argv", ["overseer-patch"]),
              mock.patch.object(patcher, "discover", return_value=self.ctx),
              mock.patch.object(patcher, "status") as status,
              mock.patch.object(patcher, "plan") as plan,
              mock.patch.object(patcher, "apply") as apply,
              mock.patch.object(patcher, "confirm_step", return_value=False)):
            self.assertEqual(patcher.main(), 0)
        status.assert_called_once_with(self.ctx)
        plan.assert_not_called()
        apply.assert_not_called()


if __name__ == "__main__":
    unittest.main()
