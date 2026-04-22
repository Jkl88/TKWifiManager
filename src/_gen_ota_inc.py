# one-shot: run from repo: py src/_gen_ota_inc.py
import pathlib
root = pathlib.Path(__file__).resolve().parent
p = (root / "ota.html").read_text(encoding="utf-8")
d = "TKWOTAEMBED99"
if f"){d}\"" in p:
    raise SystemExit("delimiter collision")
# C++ raw string: R"DELIM( ... )DELIM" ;
out = 'static const char OTA_HTML[] PROGMEM = R"' + d + "(\n" + p + "\n)" + d + '";\n'
(root / "TKWifiManager_ota.inc").write_text(out, encoding="utf-8", newline="\n")
print("OK", (root / "TKWifiManager_ota.inc").stat().st_size)
