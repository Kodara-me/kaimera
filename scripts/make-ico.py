#!/usr/bin/env python3
"""Builds assets/icons/kaimera.ico from the PNG icons, with the standard library only.

    python scripts/make-ico.py

Each PNG is stored as is inside the ICO container (supported since Windows Vista).
"""
import pathlib
import struct

SIZES = (16, 32, 48, 64, 128, 256)
ICONS = pathlib.Path(__file__).resolve().parent.parent / "assets" / "icons"


def main():
    images = [(size, (ICONS / f"kaimera-{size}.png").read_bytes()) for size in SIZES]
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = len(header) + 16 * len(images)
    entries = b""
    for size, data in images:
        entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    (ICONS / "kaimera.ico").write_bytes(header + entries + b"".join(data for _, data in images))


if __name__ == "__main__":
    main()
