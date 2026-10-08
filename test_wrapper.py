"""Checks the texture matcher without launching Overseer or modifying its files."""

import ctypes
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).parent


def le32(data, offset, value):
    data[offset:offset + 4] = value.to_bytes(4, "little")


def rgb555(color):
    r, g, b = color
    return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)


class TexturePatchTest(unittest.TestCase):
    def test_only_target_index_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            library = Path(directory) / "libtexture_patch.so"
            subprocess.run(["cc", "-shared", "-fPIC", "-std=c11", "-Wall", "-Wextra",
                            str(ROOT / "wrapper/texture_patch.c"), "-o", str(library)],
                           check=True)
            patch = ctypes.CDLL(str(library)).overseer_patch_texture
            patch.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                              ctypes.c_size_t, ctypes.c_uint, ctypes.c_uint]
            patch.restype = ctypes.c_uint

            width = height = 16
            metadata = 16
            entry = metadata + 36 * 27
            palette_offset = 1200
            pixels_offset = palette_offset + 1024
            data = bytearray(pixels_offset + width * height)
            le32(data, 8, 331)
            le32(data, 12, metadata)
            le32(data, entry, width)
            le32(data, entry + 4, height)
            le32(data, entry + 8, 8)
            le32(data, entry + 28, palette_offset)
            le32(data, entry + 32, pixels_offset)
            for i in range(256):
                color = (8 + ((i * 11) % 31) * 8,
                         8 + ((i * 7) % 31) * 8,
                         8 + ((i * 13) % 31) * 8)
                data[palette_offset + 4*i:palette_offset + 4*i + 3] = bytes(color)
            data[palette_offset + 4*24:palette_offset + 4*24 + 3] = b"\0\0\0"
            data[palette_offset + 4*255:palette_offset + 4*255 + 3] = b"\0\0\0"
            indices = [24 if i % 7 == 0 else 255 if i % 11 == 0 else i % 200
                       for i in range(width * height)]
            data[pixels_offset:] = bytes(indices)
            surface = bytearray().join(rgb555(data[palette_offset + 4*i:palette_offset + 4*i + 3])
                                        .to_bytes(2, "little") for i in indices)
            original = surface[:]
            map_buffer = ctypes.create_string_buffer(bytes(data))
            surface_buffer = ctypes.create_string_buffer(bytes(surface))
            changed = patch(map_buffer, len(data), surface_buffer, width * 2, width, height)
            expected = sum(i == 24 for i in indices)
            self.assertEqual(changed, expected)
            result = surface_buffer.raw[:len(surface)]
            for pos, index in enumerate(indices):
                pixel = int.from_bytes(result[2*pos:2*pos + 2], "little")
                before = int.from_bytes(original[2*pos:2*pos + 2], "little")
                self.assertEqual(pixel, 0x0421 if index == 24 else before)
            self.assertEqual(patch(map_buffer, len(data), surface_buffer,
                                   width * 2, width, height), 0)


if __name__ == "__main__":
    unittest.main()
