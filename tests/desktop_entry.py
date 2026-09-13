#!/usr/bin/env python3
import configparser
from pathlib import Path


entry_path = Path(__file__).resolve().parents[1] / "packaging" / "cv.vantage_browser.Vantage.desktop"
parser = configparser.ConfigParser(interpolation=None, strict=True)
parser.optionxform = str
with entry_path.open(encoding="utf-8") as entry:
    parser.read_file(entry)

main = parser["Desktop Entry"]
assert main["Type"] == "Application"
assert main["Exec"] == "vant %U"
assert main["Actions"] == "NewWindow;NewPrivateWindow;"
assert parser["Desktop Action NewWindow"]["Exec"] == "vant"
assert parser["Desktop Action NewPrivateWindow"]["Exec"] == "vant --private"
