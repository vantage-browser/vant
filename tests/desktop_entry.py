#!/usr/bin/env python3
import configparser
from pathlib import Path


entry_path = Path(__file__).resolve().parents[1] / "packaging" / "cv.vantage_browser.Vantage.desktop"
icon_path = entry_path.with_suffix(".png")
parser = configparser.ConfigParser(interpolation=None, strict=True)
parser.optionxform = str
with entry_path.open(encoding="utf-8") as entry:
    parser.read_file(entry)

main = parser["Desktop Entry"]
assert main["Type"] == "Application"
assert main["Exec"] == "vant %U"
assert main["Icon"] == icon_path.stem
assert icon_path.is_file()
assert main["Actions"] == "NewWindow;NewPrivateWindow;"
assert parser["Desktop Action NewWindow"]["Exec"] == "vant"
assert parser["Desktop Action NewPrivateWindow"]["Exec"] == "vant --private"
