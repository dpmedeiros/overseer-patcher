#!/usr/bin/env python3
"""Package the Linux patcher and its matching 32-bit Windows DLL."""

import argparse
import hashlib
import re
import struct
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FILES = {
    "overseer-patch": (ROOT / "overseer-patch", 0o755),
    "ddraw.dll": (ROOT / "wrapper/ddraw.dll", 0o644),
    "README.md": (ROOT / "README.md", 0o644),
    "wrapper/README.md": (ROOT / "wrapper/README.md", 0o644),
}


def verify_dll(data: bytes) -> None:
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise ValueError("ddraw.dll is not a Windows PE binary")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if pe + 6 > len(data) or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("ddraw.dll has an invalid PE header")
    if struct.unpack_from("<H", data, pe + 4)[0] != 0x14C:
        raise ValueError("ddraw.dll is not a 32-bit x86 binary")


def package(version: str, output: Path) -> tuple[Path, Path]:
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", version):
        raise ValueError("Version must contain only letters, digits, dots, dashes, and underscores")
    payload = {name: path.read_bytes() for name, (path, _) in FILES.items()}
    verify_dll(payload["ddraw.dll"])
    output.mkdir(parents=True, exist_ok=True)
    base = f"overseer-patcher-{version}-linux"
    archive = output / f"{base}.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED,
                         compresslevel=9) as zipped:
        for name, (_, mode) in FILES.items():
            info = zipfile.ZipInfo(f"{base}/{name}", date_time=(1980, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = (0o100000 | mode) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            zipped.writestr(info, payload[name], compress_type=zipfile.ZIP_DEFLATED,
                            compresslevel=9)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    checksums = output / "SHA256SUMS"
    checksums.write_text(f"{digest}  {archive.name}\n")
    return archive, checksums


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    archive, checksums = package(args.version, args.output)
    print(archive)
    print(checksums)


if __name__ == "__main__":
    main()
