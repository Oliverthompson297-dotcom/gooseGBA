#!/usr/bin/env python3

from pathlib import Path
import sys

WIDTH = 152
HEIGHT = 88
HEADER = b"GGP1"
PALETTE = {
    0: (255, 255, 255),
    1: (0, 0, 0),
    2: (255, 32, 32),
    3: (48, 96, 255),
    4: (48, 180, 64),
    5: (255, 220, 48),
    6: (220, 48, 220),
}


def main() -> int:
    base = Path(__file__).resolve().parent.parent
    sav_path = Path(sys.argv[1]) if len(sys.argv) > 1 else base / "cubejump.sav"
    out_path = Path(sys.argv[2]) if len(sys.argv) > 2 else base / "paint_saves" / "paint_export.ppm"

    data = sav_path.read_bytes()
    if len(data) < 16 + WIDTH * HEIGHT:
        raise SystemExit("save file is too small")
    if data[:4] != HEADER:
        raise SystemExit("save file does not contain a GooseGBA paint save")

    pixels = data[16:16 + WIDTH * HEIGHT]
    out_path.parent.mkdir(parents=True, exist_ok=True)

    with out_path.open("wb") as fh:
        fh.write(f"P6\n{WIDTH} {HEIGHT}\n255\n".encode("ascii"))
        for value in pixels:
            fh.write(bytes(PALETTE.get(value, (255, 0, 255))))

    print(out_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
