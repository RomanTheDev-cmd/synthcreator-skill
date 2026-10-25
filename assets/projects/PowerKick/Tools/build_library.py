"""Builds the 300-preset factory library of PowerKick.

  python3 build_library.py stereo-refit IN.json OUT.json   re-chooses WIDTH/DELAY/SPLIT per fitted preset
                                                           against its reference (new stereo engine)
  python3 build_library.py rows CURATED.json EXTRA.json NAMES.json  -> prints Presets.cpp rows

Curated presets keep their hand-given names; extra presets get descriptive names from their
sound (drive, length, click, rumble, width, note) and a browser group (Short/Long/Rumble/Wide/Crushed/Driven/Clean).
"""
import json, os, re, sys
from concurrent.futures import ProcessPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
if os.environ.get("PK_NUMPY_PATH"):
    sys.path.insert(0, os.environ["PK_NUMPY_PATH"])
import numpy as np

ORDER = ["tune", "decay", "punch", "body", "click", "drive", "character", "rumble", "output", "width", "delay",
         "driveMode", "charMode", "pitchDepth", "pitchTime", "clickType", "clickTone", "rumbleTime", "rumbleSync",
         "bodyTone", "pitchFall", "splitFreq"]
INT = {"driveMode", "charMode", "clickType", "rumbleSync"}
CATS = ["Techno", "Hard Techno", "Schranz", "Industrial", "Rave / Hard Dance"]
GROUPS = ["Short", "Long", "Rumble", "Wide", "Crushed", "Driven", "Clean"]
NOTES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def decay_ms(d):
    return 60.0 + 1440.0 * max(0.0, min(1.0, d)) ** 1.7


def group_of(p):
    """Browser sub-group by the most distinctive trait (keeps every group browsable)."""
    if p.get("rumble", 0) > 0.3:
        return "Rumble"
    d = decay_ms(p.get("decay", 0.45))
    if d < 300:
        return "Short"
    if d >= 450:
        return "Long"
    if p.get("width", 0) >= 50:
        return "Wide"
    drive = p.get("drive", 0.3)
    return "Crushed" if drive >= 0.75 else ("Driven" if drive >= 0.5 else "Clean")


def note_of(p):
    n = 33 + int(round(p.get("tune", 0)))  # TUNE 0 = A1 = MIDI 33
    return "%s%d" % (NOTES[n % 12], n // 12 - 1)


def name_of(p):
    drive, mode = p.get("drive", 0.3), int(round(p.get("driveMode", 0)))
    if drive < 0.25: adj = "Clean"
    elif drive < 0.5: adj = "Warm"
    elif drive < 0.75: adj = "Clipped" if mode == 1 else ("Grinding" if mode == 2 else "Driven")
    else: adj = "Grinding" if mode == 2 else "Crushed"
    if p.get("rumble", 0) > 0.3: noun = "Rumble"
    elif int(round(p.get("clickType", 1))) == 2 and p.get("click", 0) > 0.6: noun = "Clang"
    elif p.get("click", 0) > 0.75: noun = "Click"
    elif decay_ms(p.get("decay", 0.45)) < 260: noun = "Knock"
    elif p.get("pitchFall", 0) > 6: noun = "Drop"
    elif p.get("body", 0.5) > 0.75: noun = "Boom"
    elif p.get("punch", 0.5) > 0.8: noun = "Punch"
    else: noun = "Thump"
    wide = "Wide " if p.get("width", 0) >= 50 else ""
    return "%s%s %s %s" % (wide, adj, noun, note_of(p))


def lit(k, v):
    if k in INT:
        return str(int(round(v)))
    return "%.3ff" % v if k not in ("tune", "output", "pitchDepth", "pitchTime", "width", "delay", "pitchFall", "splitFreq") else "%.1ff" % v


#==============================================================================
def _stereo_job(job):
    from fit_kicks import Renderer
    from kick_features import distance
    params, target = job
    r = Renderer()
    best, bc = dict(params), None
    for w in (0, 20, 40, 60, 80, 100):
        for d in ((0,) if w == 0 else (0, 1, 3, 6, 10)):
            for sp in ((500,) if w == 0 else (250, 400, 600, 1000, 1600)):
                q = {**params, "width": float(w), "delay": float(d), "splitFreq": float(sp), "output": -12.0}
                f = r.feat(q)
                c = distance(f, target)
                if bc is None or c < bc:
                    bc, best = c, {**params, "width": float(w), "delay": float(d), "splitFreq": float(sp)}
    r.p.kill()
    return best, bc


def stereo_refit(inp, out):
    from fit_kicks import load_refs
    refs = {x["file"]: x["feat"] for x in load_refs()}
    items = json.load(open(inp))
    jobs = [(m["params"], refs[m["medoid"]]) for m in items]
    with ProcessPoolExecutor(max_workers=int(os.environ.get("FIT_WORKERS", 9))) as ex:
        res = list(ex.map(_stereo_job, jobs, chunksize=4))
    for m, (p, c) in zip(items, res):
        m["params"], m["distance_stereo"] = p, c
    json.dump(items, open(out, "w"), indent=1, ensure_ascii=False)
    print("stereo refit: %d presets" % len(items))


def rows(curated_json, extra_json, names_json, presets_cpp):
    names = json.load(open(names_json))
    entries = []
    for m in json.load(open(curated_json)):
        key = m["medoid"].split("/")[-1]
        entries.append({"name": names[key], "category": m["category"], "params": m["params"], "curated": True,
                        "comment": "fit %.1f to %s" % (m["distance"], key)})
    # Hand-made rows (no reference target) are kept from the current Presets.cpp.
    src = open(presets_cpp).read()
    fitted_names = {e["name"] for e in entries}
    for mm in re.finditer(r'\{ "([^"]+)",\s+"([^"]+)",\s+\{ ([^}]*) \}', src):
        if mm.group(1) in fitted_names:
            continue
        vals = [float(x.strip().rstrip("f")) for x in mm.group(3).split(",")]
        if len(vals) != len(ORDER):
            continue
        p = dict(zip(ORDER, vals))
        if mm.group(2) in ("Industrial", "Rave / Hard Dance") and not any(e["name"] == mm.group(1) for e in entries):
            entries.append({"name": mm.group(1), "category": mm.group(2), "params": p, "curated": True, "comment": "hand-made"})
    used = {e["name"] for e in entries}
    for m in json.load(open(extra_json)):
        p = m["params"]
        n = name_of(p)
        base, k = n, 2
        while n in used:
            n = "%s %s" % (base, ["II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"][min(k - 2, 8)] if k < 11 else str(k))
            k += 1
        used.add(n)
        entries.append({"name": n, "category": m["category"], "params": p, "curated": False,
                        "comment": "fit %.1f to %s" % (m["distance"], m["medoid"].split("/")[-1])})
    out = []
    for c in CATS:
        es = [e for e in entries if e["category"] == c]
        es.sort(key=lambda e: (GROUPS.index(group_of(e["params"])), not e["curated"], e["name"]))
        out.append("    // --- %s (%d) ---" % (c, len(es)))
        for e in es:
            p = dict(e["params"])
            p.setdefault("delay", 0.0); p.setdefault("rumbleSync", 0); p.setdefault("splitFreq", 500.0)
            p.setdefault("bodyTone", 1.0); p.setdefault("pitchFall", 0.0)
            if p.get("width", 0) < 3:
                p["width"], p["delay"] = 0.0, 0.0
            vals = ", ".join(lit(k, p[k]) for k in ORDER)
            out.append('    { %-28s %-20s { %s }, "%s" }, // %s' % ('"%s",' % e["name"], '"%s",' % c, vals, group_of(p), e["comment"]))
    print("\n".join(out))
    print("// total %d" % len(entries), file=sys.stderr)


if __name__ == "__main__":
    if sys.argv[1] == "stereo-refit":
        stereo_refit(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "rows":
        rows(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
