# Overseer DirectDraw texture wrapper

This wrapper fixes the office phone and cabinet transparency without changing
`DATA/R01/R01.MAP` on disk. It exports the DirectDraw entry points Overseer
uses, forwards them to Proton's built-in DirectDraw, and hooks
`IDirect3DTexture2::Load`. Immediately before that call copies a texture, the
wrapper corrects the seven full-size office source surfaces and their 14
attached mipmaps. The remaining black pixels keep the game's color key, so
the curtain renders as it does with the known palette patch.

The correction masks were derived by capturing the same DirectDraw surfaces
with the original and palette-patched maps. Each mask is applied only when
the on-disk map has the tested original hash and the source surface has the
expected size and pixel hash. An unknown map or surface is left alone. The
already patched map is recognized and receives no corrections.

## Build and install

On the tested Linux environment, with Clang, LLD, and Wine's 32-bit Windows
headers and import libraries installed:

```sh
./wrapper/build.sh
python3 -m unittest -v test_wrapper
```

With Steam and Overseer closed, copy `wrapper/ddraw.dll` beside
`OVERSEER.EXE`, restore the tested original `DATA/R01/R01.MAP`, and set the
game's Steam launch option to include
`WINEDLLOVERRIDES="ddraw=n,b" %command%`. To uninstall the wrapper, close the
game, remove that DLL, and remove the override from the launch option. The
map needs no wrapper-specific restoration because this DLL never writes it.

## Verification

The generated masks reproduce the patched-map captures exactly: 111 base
surfaces and 218 attached mipmaps were compared, with 783,828 pixels changed
across 21 surfaces. The unit test uses real captured office textures and
checks that an altered texture is not matched. In the tested Steam/Proton
installation, the user confirmed that the phone, cabinets, and curtain look
correct from different viewing angles with the original map and wrapper.

`generate_masks.py` regenerates `texture_masks.c` from four diagnostic
captures: original and patched base surfaces, followed by original and patched
mipmaps. Those full captures are development data and are not needed by the
installed DLL. The small compressed fixtures used by `test_wrapper.py` are in
`wrapper/fixtures`.
