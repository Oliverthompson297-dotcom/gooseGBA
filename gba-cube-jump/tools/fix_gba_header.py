#!/usr/bin/env python3

import sys
from pathlib import Path


def fit_ascii(text: str, length: int) -> bytes:
    encoded = text.encode("ascii", "ignore")[:length]
    return encoded.ljust(length, b" ")


def main() -> int:
    if len(sys.argv) != 5:
        print("usage: fix_gba_header.py <rom.gba> <title> <game_code> <maker_code>", file=sys.stderr)
        return 1

    rom_path = Path(sys.argv[1])
    rom = bytearray(rom_path.read_bytes())

    if len(rom) < 0xC0:
        print("ROM is too small to contain a valid GBA header", file=sys.stderr)
        return 1

    rom[0xA0:0xAC] = fit_ascii(sys.argv[2], 12)
    rom[0xAC:0xB0] = fit_ascii(sys.argv[3], 4)
    rom[0xB0:0xB2] = fit_ascii(sys.argv[4], 2)
    rom[0xB2] = 0x96
    rom[0xBC] = 0

    checksum = 0
    for value in rom[0xA0:0xBD]:
        checksum = (checksum - value) & 0xFF
    rom[0xBD] = (checksum - 0x19) & 0xFF

    rom_path.write_bytes(rom)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
