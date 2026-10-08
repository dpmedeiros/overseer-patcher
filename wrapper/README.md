# Overseer DirectDraw alpha wrapper

Overseer loads 8-bit indexed textures, but the tested Proton setup offers
true-color Direct3D texture formats. Overseer normally selects X1R5G5B5,
where dark palette colors and literal black can both become pixel `0x0000`.
When the renderer uses black as a color key, visible dark details disappear.
The old office-specific texture masks fixed the phone and cabinets but missed
the chair and could not cover other rooms.

A **color key** is a rule on a surface: a pixel whose RGB value falls in the
key's range should be transparent. **Alpha** stores transparency in each
pixel. They are independent; a black pixel can be opaque or transparent.
During the 8-bit palette to 16-bit conversion, several colors can become the
same value. In a captured cabinet texture, `(1,1,1)` and `(2,2,1)` both became
`0x0000`; `(3,3,3)` became `0x0421`. Some chair pixels are literal `(0,0,0)`
already. Once these values share `0x0000`, RGB alone cannot say which pixels
were meant to show.

This `ddraw.dll` proxy forwards Overseer's DirectDraw entry points to Proton.
During `IDirect3DDevice2::EnumTextureFormats`, it selects A1R5G5B5 if that
format is offered. Before `IDirect3DTexture2::Load`, it fills the alpha bit of
the source texture and every attached mipmap. All pixels are opaque for a
surface without a source color key. For a keyed surface, pixels in the key
range remain transparent. RGB values and game files are unchanged. If
A1R5G5B5 is unavailable, format enumeration is passed through unchanged.
For example, an opaque black pixel becomes `0x8000` (alpha 1), while keyed
black stays `0x0000` (alpha 0). The alpha bit carries the distinction that
RGB555 lacks. A keyed surface that uses the exact key color for both an opaque
detail and transparency remains ambiguous; that case has not been observed
in the office test.

The fix uses DirectDraw's per-surface color key and has no room or texture
index list. On the tested installation, the user confirmed that the chair,
phone, cabinets, and curtain look correct with the original office map.
Other rooms still need visual playtesting.

## Build and install

On the tested Linux environment, with Clang, LLD, and Wine's 32-bit Windows
headers and import libraries installed:

```sh
./wrapper/build.sh
python3 -m unittest -v test_alpha_patch
```

The [packaged installer](../README.md) copies the DLL beside `OVERSEER.EXE`
and sets an Overseer-only Wine registry override of `ddraw=native,builtin`.
For a manual test, copy the DLL beside `OVERSEER.EXE` and use the Steam launch
option `WINEDLLOVERRIDES="ddraw=n,b" %command%`. Remove the DLL and override
to undo that manual setup. The wrapper does not write game data.

`map_lzw.py` and `RESEARCH.md` document the palette analysis. The earlier
office mask generator, masks, fixtures, and `test_wrapper.py` remain in the
repository for comparison; they are not linked into this DLL.
