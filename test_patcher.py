"""End-to-end fixture test; never writes to the real Steam installation."""

import importlib.machinery
import importlib.util
import shutil
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).with_name("overseer-patch")
loader = importlib.machinery.SourceFileLoader("overseer_patch", str(SCRIPT))
spec = importlib.util.spec_from_loader(loader.name, loader)
patcher = importlib.util.module_from_spec(spec)
import sys
sys.modules[spec.name] = patcher
loader.exec_module(patcher)


class PatchFixture(unittest.TestCase):
    def test_missing_proton_downloads_verified_archive(self):
        with tempfile.TemporaryDirectory(prefix="overseer-proton-test-") as name:
            root = Path(name)
            payload = root / "payload/Proton-CachyOS"
            payload.mkdir(parents=True)
            (payload / "proton").write_text("#!/bin/sh\n")
            (payload / "version").write_text(patcher.RELEASE_TAG)
            (payload / "compatibilitytool.vdf").write_text('"compatibilitytools" {}')
            archive = root / "release.tar.xz"
            with tarfile.open(archive, "w:xz") as bundle:
                bundle.add(payload, arcname=payload.name)
            digest = "sha256:" + patcher.file_sha(archive)
            steam = root / "Steam"
            with mock.patch.object(patcher, "choose_proton", side_effect=patcher.PatchError("missing")):
                with mock.patch.object(patcher, "release_assets", return_value=(archive.as_uri(), digest, None)):
                    installed = patcher.install_proton(steam)
            self.assertEqual((installed / "version").read_text(), patcher.RELEASE_TAG)
            self.assertTrue((installed / "proton").is_file())

    def test_apply_and_revert_original_installation(self):
        with tempfile.TemporaryDirectory(prefix="overseer-patcher-test-") as name:
            steam = Path(name) / "Steam"
            library = steam / "steamapps"
            game = library / "common/Overseer"
            prefix = library / "compatdata/302370/pfx"
            tool = steam / "compatibilitytools.d/Proton-CachyOS Latest-x86_64_v3"
            (game / "DATA/R01").mkdir(parents=True)
            prefix.mkdir(parents=True)
            tool.mkdir(parents=True)
            (library / "appmanifest_302370.acf").write_text('"AppState"\n{\n "installdir" "Overseer"\n}\n')
            (steam / "config").mkdir()
            config = steam / "config/config.vdf"
            original_config = '"InstallConfigStore"\n{\n "CompatToolMapping"\n {\n  "0"\n  {\n   "name" "other"\n  }\n }\n}\n'
            config.write_text(original_config)
            registry = prefix / "user.reg"
            original_registry = 'WINE REGISTRY Version 2\n\n[Software\\\\Other] 123\n"Example"="keep"\n'
            registry.write_text(original_registry)
            (tool / "proton").write_text("#!/bin/sh\n")
            (tool / "version").write_text(patcher.RELEASE_TAG)
            (tool / "compatibilitytool.vdf").write_text('"compatibilitytools" { "compat_tools" { "Proton-CachyOS Latest" {} } }')
            real_game = Path.home() / ".steam/steam/steamapps/common/Overseer"
            shutil.copy2(real_game / "OVERSEER.EXE", game / "OVERSEER.EXE")
            original_map = real_game / "DATA/R01/R01.MAP.pre-cabinet-palette.bak"
            self.assertEqual(patcher.file_sha(original_map), patcher.ORIGINAL_MAP_SHA)
            shutil.copy2(original_map, game / "DATA/R01/R01.MAP")
            ctx = patcher.discover(steam)
            self.assertEqual(patcher.verify_context(ctx), (patcher.ORIGINAL_MAP_SHA, None, None))
            patcher.apply(ctx)
            self.assertEqual(patcher.verify_context(ctx), (patcher.PATCHED_MAP_SHA, "128", patcher.TOOL_ID))
            self.assertTrue(patcher.alias_path(steam).is_dir())
            patcher.revert(ctx)
            self.assertEqual(patcher.verify_context(ctx), (patcher.ORIGINAL_MAP_SHA, None, None))
            self.assertEqual(registry.read_text(), original_registry)
            self.assertEqual(config.read_text(), original_config)
            self.assertFalse(patcher.alias_path(steam).exists())

    def test_apply_to_current_patched_state(self):
        with tempfile.TemporaryDirectory(prefix="overseer-patcher-test-") as name:
            steam = Path(name) / "Steam"
            library = steam / "steamapps"
            game = library / "common/Overseer"
            prefix = library / "compatdata/302370/pfx"
            tool = steam / "compatibilitytools.d/Proton-CachyOS Latest-x86_64_v3"
            (game / "DATA/R01").mkdir(parents=True)
            prefix.mkdir(parents=True)
            tool.mkdir(parents=True)
            (library / "appmanifest_302370.acf").write_text('"AppState"\n{\n "installdir" "Overseer"\n}\n')
            (steam / "config").mkdir()
            real_steam = Path.home() / ".steam/steam"
            shutil.copy2(real_steam / "config/config.vdf", steam / "config/config.vdf")
            config = steam / "config/config.vdf"
            config.write_text(patcher.edit_mapping(config.read_text(), None))
            shutil.copy2(real_steam / "steamapps/compatdata/302370/pfx/user.reg", prefix / "user.reg")
            real_game = real_steam / "steamapps/common/Overseer"
            shutil.copy2(real_game / "OVERSEER.EXE", game / "OVERSEER.EXE")
            shutil.copy2(real_game / "DATA/R01/R01.MAP", game / "DATA/R01/R01.MAP")
            (tool / "proton").write_text("#!/bin/sh\n")
            (tool / "version").write_text(patcher.RELEASE_TAG)
            (tool / "compatibilitytool.vdf").write_text('"compatibilitytools" { "compat_tools" { "Proton-CachyOS Latest" {} } }')
            ctx = patcher.discover(steam)
            self.assertEqual(patcher.verify_context(ctx), (patcher.PATCHED_MAP_SHA, "128", None))
            before_reg = ctx.registry.read_bytes()
            before_config = ctx.config.read_bytes()
            patcher.apply(ctx)
            self.assertEqual(patcher.verify_context(ctx), (patcher.PATCHED_MAP_SHA, "128", patcher.TOOL_ID))
            patcher.revert(ctx)
            self.assertEqual(ctx.registry.read_bytes(), before_reg)
            self.assertEqual(ctx.config.read_bytes(), before_config)
            self.assertEqual(patcher.verify_context(ctx), (patcher.PATCHED_MAP_SHA, "128", None))


if __name__ == "__main__":
    unittest.main()
