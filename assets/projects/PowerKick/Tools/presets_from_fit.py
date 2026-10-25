"""Prints C++ preset rows (Presets.cpp format) from a fit_kicks.py result.

  python3 presets_from_fit.py FIT.json NAMES.json   -> rows for the fitted categories
NAMES.json maps the medoid file name to a preset name (written by hand after listening to
the numbers: length, drive, rumble...). Rows keep the parameter order of Presets.cpp.
"""
import json, sys

ORDER = ["tune", "decay", "punch", "body", "click", "drive", "character", "rumble", "output", "width", "delay",
         "driveMode", "charMode", "pitchDepth", "pitchTime", "clickType", "clickTone", "rumbleTime", "rumbleSync",
         "bodyTone", "pitchFall"]
INT = {"driveMode", "charMode", "clickType", "rumbleSync"}


def fmt(k, v):
    if k in INT:
        return str(int(round(v)))
    if k in ("tune", "output", "pitchDepth", "pitchTime", "width", "delay", "pitchFall"):
        return "%.1ff" % v
    return "%.3ff" % v


fits = json.load(open(sys.argv[1]))
names = json.load(open(sys.argv[2])) if len(sys.argv) > 2 else {}
for m in fits:
    p = dict(m["params"])
    p.setdefault("delay", 0.0)
    p.setdefault("rumbleSync", 0)
    if p.get("width", 0) < 3:   # inaudible width: store a clean mono preset
        p["width"] = 0.0
        p["delay"] = 0.0
    key = m["medoid"].split("/")[-1]
    name = names.get(key, key)
    vals = ", ".join(fmt(k, p.get(k, 0.0)) for k in ORDER)
    print('    { %-22s %-16s { %s } }, // fit %.1f to %s (cluster of %d)' % ('"%s",' % name, '"%s",' % m["category"], vals,
                                                                            m["distance"], key, m["cluster_size"]))
