# Experimental DirectDraw texture wrapper

This is an alternative to editing `DATA/R01/R01.MAP`. The Steam game supplies
X1R5G5B5 textures and a black color key to DirectDraw. Multiple near-black
palette colors from the original 8-bit map have already collapsed to the same
16-bit black pixel by that point. Changing the key alone would also remove the
curtain's intended transparency.

The proxy forwards `DirectDrawCreate` to Proton's system `ddraw.dll`. It hooks
`IDirect3DTexture2::Load`, identifies source surfaces against the tested
original `R01.MAP`, and changes only pixels corresponding to the 76 palette
entries in `overseer-patch`. Other black pixels retain the color key. The map
file on disk is never changed by this wrapper. It does nothing for a map whose
content does not match the known original, including an already patched map.

## Build

On the tested Linux environment, build the 32-bit Windows DLL with:

```sh
./wrapper/build.sh
python3 -m unittest -v test_wrapper
```

This uses Clang, LLD, and Wine's 32-bit Windows headers and import libraries.
The result is `wrapper/ddraw.dll`. It must be loaded as a native DLL, for
example with the game's Steam launch option
`WINEDLLOVERRIDES="ddraw=n,b" %command%`.

## Verification so far

The isolated matcher test confirms that matching office pixels change and
other black pixels remain black. A Proton smoke test loaded the DLL as native,
forwarded to Wine's built-in DirectDraw, and corrected all 452 targeted pixels
in texture 27 when copying a synthetic source surface. Its source is
`wrapper/smoke.c`; run `./wrapper/build.sh --smoke` to build it. A full visual
test in Overseer is still needed before this replaces the map edit in the
patcher. The real Steam installation currently has the existing map palette
patch, so this wrapper would deliberately make no texture changes there.

For a visual test without touching the installed game, create a copy-on-write
test tree on the same Btrfs filesystem:

```sh
python3 wrapper/prepare_test.py --game /path/to/Overseer \
  --original-map /path/to/original/R01.MAP
```

The helper prints a `.wrapper-test-*` directory in the repository. Launch its
`OVERSEER.EXE` with the
tested Proton tool and `WINEDLLOVERRIDES=ddraw=n,b`. A direct Proton launch
from this test tree loaded the native proxy but did not reach
`DirectDrawCreate` within 30 seconds. A normal Steam launch or a longer
interactive run is still needed for the visual office check.
