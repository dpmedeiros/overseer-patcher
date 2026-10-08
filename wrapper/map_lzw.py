"""Inspect Overseer MAP palettes and LZW-compressed 8-bit texture indices.

This is a diagnostic tool; it does not change game files.
"""

import argparse
from collections import Counter
from pathlib import Path


def u32(data, offset):
    return int.from_bytes(data[offset:offset + 4], "little")


def decode_lzw(stream, expected):
    """Decode Overseer's LSB-first, 9-to-13-bit LZW variant."""
    table = {i: bytes((i,)) for i in range(256)}
    width, next_code = 9, 258
    previous = b""
    bits = count = position = 0
    output = bytearray()
    while position < len(stream):
        while count < width:
            if position == len(stream):
                raise ValueError("Truncated LZW stream")
            bits |= stream[position] << count
            count += 8
            position += 1
        code = bits & ((1 << width) - 1)
        bits >>= width
        count -= width
        if code == 256:
            table = {i: bytes((i,)) for i in range(256)}
            width, next_code, previous = 9, 258, b""
            continue
        if code == 257:
            break
        if code in table:
            entry = table[code]
        elif code == next_code and previous:
            entry = previous + previous[:1]
        else:
            raise ValueError(f"Invalid LZW code {code} at byte {position}")
        output.extend(entry)
        if len(output) > expected:
            raise ValueError("LZW output exceeds texture size")
        if previous:
            table[next_code] = previous + entry[:1]
            next_code += 1
            if next_code == 1 << width and width < 13:
                width += 1
        previous = entry
    if len(output) != expected:
        raise ValueError(f"LZW size mismatch: {len(output)}/{expected}")
    return bytes(output)


def textures(path):
    data = path.read_bytes()
    if u32(data, 0) != len(data):
        raise ValueError("Invalid MAP length")
    count, metadata = u32(data, 8), u32(data, 12)
    if metadata + 36 * count > len(data):
        raise ValueError("Invalid MAP directory")
    for number in range(count):
        entry = metadata + number * 36
        width, height, depth = (u32(data, entry + offset) for offset in (0, 4, 8))
        palette, pixels = (u32(data, entry + offset) for offset in (28, 32))
        end = (u32(data, metadata + (number + 1) * 36 + 28)
               if number + 1 < count else len(data))
        if depth != 8 or pixels - palette != 1024:
            raise ValueError(f"Unexpected metadata for texture {number}")
        yield number, width, height, data[palette:pixels], data[pixels:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("map", type=Path)
    parser.add_argument("textures", nargs="*", type=int)
    args = parser.parse_args()
    selected = set(args.textures)
    for number, width, height, palette, compressed in textures(args.map):
        if selected and number not in selected:
            continue
        indices = decode_lzw(compressed, width * height)
        used = Counter(indices)
        collapse = [(i, used[i], tuple(palette[4 * i:4 * i + 3]))
                    for i in used if max(palette[4 * i:4 * i + 3]) <= 2]
        literal = sum(count for _, count, rgb in collapse if rgb == (0, 0, 0))
        print(number, width, height, "used", len(used), "index255", used[255],
              "black", literal, "nonzero_collapsed",
              sum(count for _, count, rgb in collapse if rgb != (0, 0, 0)),
              "details", sorted(collapse))


if __name__ == "__main__":
    main()
