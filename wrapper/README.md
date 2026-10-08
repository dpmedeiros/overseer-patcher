# Overseer DirectDraw alpha wrapper

Overseer loads 8-bit indexed textures, but the tested Proton setup offers
true-color Direct3D texture formats. Overseer normally selects X1R5G5B5,
where dark palette colors and literal black can both become pixel `0x0000`.
When the renderer uses black as a color key, visible dark details disappear.
The old office-specific texture masks fixed the phone and cabinets but missed
the chair and could not cover other rooms.

This `ddraw.dll` proxy forwards Overseer's DirectDraw entry points to Proton.
During `IDirect3DDevice2::EnumTextureFormats`, it selects A1R5G5B5 if that
format is offered. Before `IDirect3DTexture2::Load`, it fills the alpha bit of
the source texture and every attached mipmap. All pixels are opaque for a
surface without a source color key. For a keyed surface, pixels in the key
range remain transparent. RGB values and game files are unchanged. If
A1R5G5B5 is unavailable, format enumeration is passed through unchanged.

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

With Steam and Overseer closed, copy `wrapper/ddraw.dll` beside
`OVERSEER.EXE`, restore the original game map, and set the Steam launch
option to include `WINEDLLOVERRIDES="ddraw=n,b" %command%`. To uninstall,
close the game, remove that DLL, and remove the override. The wrapper does
not write game data.

`map_lzw.py` and `RESEARCH.md` document the palette analysis. The earlier
office mask generator, masks, fixtures, and `test_wrapper.py` remain in the
repository for comparison; they are not linked into this DLL.
