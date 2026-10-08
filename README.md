# Tex Murphy: Overseer patcher for Linux

This installer fixes Overseer's FMV playback and texture transparency issues in the steam installation of Overseer on Linux (App ID `302370`). It bundles a 32-bit DirectDraw wrapper,
configures Overseer's Wine prefix, and selects a pinned Proton-CachyOS release.

**Disclosure:** This project was developed primarily by an AI coding agent,
with human direction and testing.

## Install

**Warning:** If Proton-CachyOS `cachyos-11.0-20261005-slr` is not already
installed, the patcher will download, verify, and install that release when you
apply its changes.

1. Install Overseer through Steam and launch it once to create its Wine prefix.
2. Download the `overseer-patcher-<version>-linux.zip` archive from this
   repository's GitHub Releases page and extract it.
3. Close Overseer and Steam. Run `./overseer-patch` from the extracted directory.
   If your ZIP extractor drops executable permissions, run
   `python3 overseer-patch` instead. The installer shows status, asks to continue,
   shows its plan, then asks before applying.
4. Restart Steam. The selected compatibility tool should read
   `Overseer Proton-CachyOS cachyos-11.0-20261005-slr`.

Use `./overseer-patch revert` with Steam closed to restore the pre-install DLL,
registry values, and per-game compatibility mapping.
For a nonstandard Steam root, pass `--steam-root /path/to/Steam`. Python 3.12 or
newer is required.

## Why the DirectDraw wrapper works

Overseer's textures contain 8-bit pixel indices. Each index selects one of 256
palette entries; each entry stores three 8-bit color values (red, green, and
blue) plus an unused fourth byte. That gives each palette color **24 bits of
RGB**. Overseer converts these colors to the 16-bit X1R5G5B5 texture format,
which has only **15 bits of RGB**: five bits for each color channel, plus one
unused bit. Reducing 24 color bits to 15 groups nearby colors together. In a
captured cabinet texture, palette colors `(1,1,1)` and `(2,2,1)` both became
`0x0000`, the same RGB value as literal black. Once converted, the RGB value
alone cannot tell these dark details apart from black background pixels.

A **color key** is a rule attached to a surface: pixels in its keyed RGB range
should be transparent. **Alpha** records transparency in each pixel. The
wrapper selects A1R5G5B5 when Proton offers it. This format still has 15 RGB
bits, but uses the remaining bit for alpha. The wrapper fills that bit for each
pixel from the source surface's color key, including attached mipmaps: opaque
black becomes `0x8000`, while transparent keyed black remains `0x0000`. On a
surface without a color key, every pixel is opaque. This preserves visibility
despite the RGB quantization; it does not restore the discarded color detail.
The wrapper works across textures without room or palette-index lists and does
not change game files. See [wrapper/README.md](wrapper/README.md) for the
technical details and current limits.

## Changes made by the installer

- Copies the packaged `ddraw.dll` beside `OVERSEER.EXE` and sets the
  `OVERSEER.EXE` Wine `ddraw` override to `native,builtin` in this game prefix.
- Sets `VideoMemorySize=128` for `OVERSEER.EXE` in the same prefix, if unset.
  An existing value other than `128` causes the installer to stop.
- Adds a per-game Steam `CompatToolMapping` for App ID `302370`, pointing to a
  unique alias of Proton-CachyOS `cachyos-11.0-20261005-slr`.

Backups and a journal are stored in
`steamapps/compatdata/302370/overseer-patcher/wrapper`. Revert checks the
files it owns before restoring them. The wrapper has been
visually checked in Tex's office, including the chair, phone, cabinets, and
curtain; other rooms still need playtesting.

## Build and release

On Linux, install Clang, LLD, Wine's Windows development headers, and 32-bit
Windows import libraries. Then run:

```sh
make check
make dist VERSION=v0.1.0
(cd dist && sha256sum -c SHA256SUMS)
```

`make dist` creates a versioned ZIP containing the executable
`overseer-patch`, compiled `ddraw.dll`, and both READMEs, plus `SHA256SUMS`.
Pushing a `v*` tag runs the [release workflow](.github/workflows/release.yml),
which builds, tests, packages, and attaches these files to a GitHub release.
Run `make clean` to remove generated build and release files.

The installer checks the tested executable signature:

| File | SHA-256 |
| --- | --- |
| `OVERSEER.EXE` | `5635781f506e953b06df0e28aad2c810bb02d2d64470a158614cbed927e43cfc` |

GOG and DVD installations are outside this release's scope.

## WIP
This is a work-in-progress. I am still looking for issues that popup and resolving them with
future revisions of this patcher as I go. Let me know of anything you find by filing an issue.