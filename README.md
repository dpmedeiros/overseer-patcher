# Tex Murphy: Overseer Steam patcher for Linux
Historically, Tex Murphy: Overseer is the least playable of the Tex Murphy titles on modern systems. The main issues include FMV codec incompatibilities and texture load problems.

The `ddraw-wrapper` branch also contains a [general texture wrapper](wrapper/README.md)
that works with the original room maps. The `overseer-patch` script below still
applies the older office-specific map patch; do not apply that map patch when
testing the wrapper with clean game data.

`overseer-patch` is an executable Python 3 script that patches the game to fix these issues for Linux systems. Its shell entry point gives a clear error if Python is missing. See [Changes](#changes) for the changes this patcher makes to the game's Wine prefix, game data, and Steam configuration.

**IMPORTANT** Note that the patcher will download and install Proton-CachyOS `cachyos-11.0-20261005-slr` on your system if it does not exist. This version of Proton is needed to provide the codec used by some of the game's FMVs.

## Use

To use this patch:
1. Install Overseer from Steam
2. Launch it once so that the prefix for the game gets made. If it outright fails to start at all, this patch may fix that by forcing the game to use Proton-CachyOS.
3. Close Overseer and Steam.
4. From this directory:

```sh
./overseer-patch
```
The tool prompts before showing status, showing the plan, and applying the changes. Answer `y` at each prompt to continue. Any other answer, Enter, or Ctrl-C stops before the remaining steps.

Restart Steam after applying so it discovers the game-specific Proton alias. In Steam Properties, the selected compatibility tool should read `Overseer Proton-CachyOS cachyos-11.0-20261005-slr` after restart.

Run `./overseer-patch revert` with Steam closed to undo the changes this patcher made. Backups and a journal are kept under this game's `steamapps/compatdata/302370/overseer-patcher` directory. The script checks current files before changing or reverting them. Existing texture and registry fixes are recognized and preserved by revert if they predate the patcher.

## WIP
This patcher remains a work in progress. The tested setup fixes the observed FMV and texture issues in Tex's office; later rooms have not been checked.

Let me know if you still find bugs by filing an issue. I also plan on adding support for the GOG release at some point.

## Changes

- Sets `VideoMemorySize=128` for `OVERSEER.EXE` under this game's prefix `HKCU\Software\Wine\AppDefaults\OVERSEER.EXE\Direct3D` if it is unset. An existing value other than `128` causes a stop.
- Changes 228 bytes of `DATA/R01/R01.MAP`, limited to palette colors for the office phone and filing cabinets. This avoids color-key transparency while retaining the curtain's own transparency. The original map is backed up before modification.
- Adds a per-game `CompatToolMapping` for App ID `302370` in the Steam user's `config.vdf`, pointing to the pinned Proton-CachyOS alias. No global mapping is changed.

## Supported signatures

| File | SHA-256 |
| --- | --- |
| Original `OVERSEER.EXE` | `5635781f506e953b06df0e28aad2c810bb02d2d64470a158614cbed927e43cfc` |
| Original `R01.MAP` | `a9a91e1c53ecaada8b709141d626fe946ee6f71c555238397302bc2f244c46ca` |
| Patched `R01.MAP` | `09b88371443c7beb97d36fa03eec948da59bd500d2667dcb6268c9d51d203766` |

For a nonstandard Steam root, pass `--steam-root /path/to/Steam`. GOG and DVD installations are currently outside this version's scope.
