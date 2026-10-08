# Texture color-key investigation

The earlier wrapper fixed only textures that changed in a comparison with the
hand-patched office map. The chair still had transparent dark areas, so those
masks were incomplete.

`map_lzw.py` reads the original MAP palettes and decodes their 9-to-13-bit
LSB-first LZW index streams. It does not modify the game. All 10,629 textures
in the 31 installed room maps decoded successfully.

The office chair textures use literal palette black for some visible pixels:
texture 0 uses index 90, texture 3 uses index 53, and texture 6 uses index 52.
These are `(0, 0, 0)` before any RGB555 conversion. Cabinet texture 198
contains both literal black at index 51 and nonzero `(1, 1, 1)` at index 48;
both appear as pixel `0x0000` in the captured RGB555 source surface. In the
same texture, `(2, 2, 1)` also becomes zero, while `(3, 3, 3)` becomes
`0x0421`. This is measured from the original map and the captured surface,
not a claim about all possible display formats.

In the installed maps, 114,693,634 decoded texels use literal `(0, 0, 0)`;
1,426,478 more use nonzero palette triples with all channels at most 2.
Those counts include legitimate transparent pixels and opaque dark pixels.
Palette index 255 is not a universal transparency marker: some textures use
it as pixel data, and chair textures use other black indices. The palette's
fourth byte is zero for the sampled office textures, including both visible
dark texels and transparent backgrounds.

The diagnostic `OVERSEER_TRACE_KEYS=1` logs `DDCKEY_SRCBLT` for each source and
target texture at `IDirect3DTexture2::Load`. Some office textures have a
surface color key of zero; many do not. Chair textures 0-11 and the phone
texture in capture 9 have no source surface key at load time, despite the
reported visual transparency. A surface key check alone is therefore not yet
sufficient to decide which black texels should be preserved.

Proton enumerated X1R5G5B5, A1R5G5B5, A4R4G4B4, R5G6B5, X8R8G8B8, and
A8R8G8B8, with no palettized texture format. Forcing A1R5G5B5 alone left
every source alpha bit clear: Overseer writes 16-bit RGB values but does not
populate the alpha bit. The new wrapper chooses A1R5G5B5 and populates that
bit from the surface's color-key state before each texture load, including
attached mipmaps. The user reported that the chair, phone, cabinets, and
curtain all look correct in the office with the original map. This is a
general rule based on API state rather than room or texture identities, but
other rooms have not yet been visually checked.
