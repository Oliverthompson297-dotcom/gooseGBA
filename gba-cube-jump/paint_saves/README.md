Paint exports go here.

The GBA app saves paint data into SRAM, which most emulators write next to the ROM as `cubejump.sav`.

To export that save into an image file in this folder, run:

```sh
cd "/Users/oliverlegrys/Documents/New project/gba-cube-jump"
python3 tools/export_paint_save.py
```
