"""Create a copy-on-write game tree for a visual wrapper test."""

import argparse
import hashlib
import shutil
import subprocess
import tempfile
from pathlib import Path

ORIGINAL_MAP_SHA = "a9a91e1c53ecaada8b709141d626fe946ee6f71c555238397302bc2f244c46ca"
ORIGINAL_EXE_SHA = "5635781f506e953b06df0e28aad2c810bb02d2d64470a158614cbed927e43cfc"


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def prepare(game, original_map, wrapper):
    if sha256(game / "OVERSEER.EXE") != ORIGINAL_EXE_SHA:
        raise ValueError("OVERSEER.EXE is not the tested build")
    if sha256(original_map) != ORIGINAL_MAP_SHA:
        raise ValueError("Original R01.MAP does not match the tested build")
    if not wrapper.is_file():
        raise ValueError("Build wrapper/ddraw.dll first")
    output = Path(tempfile.mkdtemp(prefix=".wrapper-test-", dir=Path(__file__).parent.parent))
    try:
        subprocess.run(["cp", "-a", "--reflink=always", str(game) + "/.", str(output)], check=True)
        subprocess.run(["cp", "-a", "--reflink=always", str(original_map),
                        str(output / "DATA/R01/R01.MAP")], check=True)
        shutil.copy2(wrapper, output / "ddraw.dll")
    except Exception:
        shutil.rmtree(output)
        raise
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, required=True, help="installed Overseer directory")
    parser.add_argument("--original-map", type=Path, required=True, help="original R01.MAP or verified backup")
    args = parser.parse_args()
    output = prepare(args.game.resolve(), args.original_map.resolve(),
                     Path(__file__).with_name("ddraw.dll"))
    print(output)


if __name__ == "__main__":
    main()
