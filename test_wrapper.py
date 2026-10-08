"""Verify the wrapper against an actual office texture captured from Overseer."""

import ctypes
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).parent
FIXTURES = ROOT / "wrapper/fixtures"


class TexturePatchTest(unittest.TestCase):
    def test_matches_real_office_texture_and_rejects_changes(self):
        original = zlib.decompress((FIXTURES / "office_9_original.rgb555.z").read_bytes())
        expected = zlib.decompress((FIXTURES / "office_9_patched.rgb555.z").read_bytes())
        self.assertEqual(len(original), 256 * 128 * 2)
        self.assertEqual(len(expected), len(original))
        with tempfile.TemporaryDirectory() as directory:
            library = Path(directory) / "libtexture_patch.so"
            subprocess.run(
                ["cc", "-shared", "-fPIC", "-std=c11", "-Wall", "-Wextra",
                 str(ROOT / "wrapper/texture_patch.c"),
                 str(ROOT / "wrapper/texture_masks.c"), "-o", str(library)],
                check=True,
            )
            patch = ctypes.CDLL(str(library)).overseer_patch_texture
            patch.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                              ctypes.c_uint, ctypes.c_uint, ctypes.c_uint]
            patch.restype = ctypes.c_uint

            surface = ctypes.create_string_buffer(original)
            self.assertEqual(patch(surface, 512, 256, 128, 0), 12262)
            self.assertEqual(surface.raw[:len(expected)], expected)
            self.assertEqual(patch(surface, 512, 256, 128, 0), 0)

            altered = bytearray(original)
            altered[0] ^= 1
            surface = ctypes.create_string_buffer(bytes(altered))
            self.assertEqual(patch(surface, 512, 256, 128, 0), 0)
            self.assertEqual(surface.raw[:len(altered)], bytes(altered))

            mip = zlib.decompress((FIXTURES / "office_mip18_original.rgb555.z").read_bytes())
            mip_expected = zlib.decompress((FIXTURES / "office_mip18_patched.rgb555.z").read_bytes())
            surface = ctypes.create_string_buffer(mip)
            self.assertEqual(patch(surface, 256, 128, 64, 1), 2956)
            self.assertEqual(surface.raw[:len(mip_expected)], mip_expected)

            blank = zlib.decompress((FIXTURES / "office_mip22_original.rgb555.z").read_bytes())
            filled = zlib.decompress((FIXTURES / "office_mip22_patched.rgb555.z").read_bytes())
            surface = ctypes.create_string_buffer(blank)
            self.assertEqual(patch(surface, 32, 16, 16, 0), 0)
            self.assertEqual(surface.raw[:len(blank)], blank)
            self.assertEqual(patch(surface, 32, 16, 16, 1), 256)
            self.assertEqual(surface.raw[:len(filled)], filled)


if __name__ == "__main__":
    unittest.main()
