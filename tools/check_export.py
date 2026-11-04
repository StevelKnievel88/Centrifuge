"""Checks a Centrifuge USB / XML export with pyrekordbox (independent of Centrifuge's own reader).

usage: python tools/check_export.py <export-test output folder>
"""
import glob
import os
import sys
import types

# PYREKORDBOX_SRC: use a source checkout without its database dependencies (only construct + bidict needed).
if os.environ.get("PYREKORDBOX_SRC"):
    pkg = types.ModuleType("pyrekordbox")
    pkg.__path__ = [os.path.join(os.environ["PYREKORDBOX_SRC"], "pyrekordbox")]
    sys.modules["pyrekordbox"] = pkg

from pyrekordbox.anlz import AnlzFile
from pyrekordbox.rbxml import RekordboxXml

out = sys.argv[1]
stick = os.path.join(out, "stick")
ok = True

anlz = sorted(glob.glob(os.path.join(stick, "PIONEER", "USBANLZ", "*", "*", "ANLZ0000.*")))
if not anlz:
    sys.exit("no analysis files were written")

for path in anlz:
    a = AnlzFile.parse_file(path)
    tags = a.tag_types
    print(os.path.relpath(path, stick), tags)
    if "PPTH" not in tags:
        ok = False
        print("  missing PPTH")
    ppth = a.get_tag("PPTH").content.path
    if not os.path.exists(os.path.join(stick, ppth.lstrip("/"))):
        ok = False
        print("  PPTH points at a missing file:", ppth)
    if path.endswith(".DAT") and "PQTZ" not in tags:
        ok = False
        print("  missing beat grid")
    if path.endswith(".EXT"):
        hot = [t for t in a.tags if t.type == "PCO2" and str(t.content.type) == "hotcue"]
        names = [e.comment for t in hot for e in t.content.entries]
        print("  hot cue names:", names)
        if "Déjà vu" not in names:
            ok = False
            print("  hot cue name lost")
        for t in a.tags:
            if t.type == "PCO2" and str(t.content.type) == "memory" and len(t.content.entries) > 10:
                ok = False
                print("  more memory cues than rekordbox holds")

xml = RekordboxXml(os.path.join(out, "rekordbox.xml"))
tracks = xml.get_tracks()
print("xml tracks:", len(tracks))
if len(tracks) == 0:
    ok = False
for t in tracks:
    marks = t.marks
    tempos = t.tempos
    print(" ", t["Name"], "bpm", t["AverageBpm"], "marks", len(marks), "tempos", len(tempos))
    if not any(m["Num"] == 2 for m in marks):
        ok = False
        print("  hot cue C missing from the XML")

if not ok:
    sys.exit("export check failed")
print("export check passed")
