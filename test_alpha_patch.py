"""Verify that source color keys become per-pixel alpha without changing RGB."""

import ctypes
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).parent


class AlphaPatchTest(unittest.TestCase):
    def test_opaque_black_and_keyed_black_are_distinct(self):
        with tempfile.TemporaryDirectory() as directory:
            library = Path(directory) / "libalpha_patch.so"
            subprocess.run(
                ["cc", "-shared", "-fPIC", "-std=c11", "-Wall", "-Wextra",
                 str(ROOT / "wrapper/alpha_patch.c"), "-o", str(library)],
                check=True,
            )
            patch = ctypes.CDLL(str(library)).overseer_apply_alpha
            patch.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint,
                              ctypes.c_uint, ctypes.c_int, ctypes.c_uint16,
                              ctypes.c_uint16]
            patch.restype = ctypes.c_uint

            original = struct.pack("<4H", 0, 0x0421, 0, 0x0842)
            opaque = ctypes.create_string_buffer(original)
            self.assertEqual(patch(opaque, 4, 2, 2, 0, 0, 0), 4)
            self.assertEqual(opaque.raw[:8],
                             struct.pack("<4H", 0x8000, 0x8421, 0x8000, 0x8842))
            self.assertEqual(patch(opaque, 4, 2, 2, 0, 0, 0), 0)

            keyed = ctypes.create_string_buffer(original)
            self.assertEqual(patch(keyed, 4, 2, 2, 1, 0, 0), 2)
            self.assertEqual(keyed.raw[:8],
                             struct.pack("<4H", 0, 0x8421, 0, 0x8842))

    def test_key_range_and_row_padding(self):
        with tempfile.TemporaryDirectory() as directory:
            library = Path(directory) / "libalpha_patch.so"
            subprocess.run(
                ["cc", "-shared", "-fPIC", "-std=c11", "-Wall", "-Wextra",
                 str(ROOT / "wrapper/alpha_patch.c"), "-o", str(library)],
                check=True,
            )
            patch = ctypes.CDLL(str(library)).overseer_apply_alpha
            patch.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint,
                              ctypes.c_uint, ctypes.c_int, ctypes.c_uint16,
                              ctypes.c_uint16]
            patch.restype = ctypes.c_uint
            surface = ctypes.create_string_buffer(struct.pack("<6H", 1, 2, 0x1234,
                                                               3, 4, 0x5678))
            self.assertEqual(patch(surface, 6, 2, 2, 1, 2, 3), 2)
            self.assertEqual(surface.raw[:12],
                             struct.pack("<6H", 0x8001, 2, 0x1234,
                                         3, 0x8004, 0x5678))


if __name__ == "__main__":
    unittest.main()
