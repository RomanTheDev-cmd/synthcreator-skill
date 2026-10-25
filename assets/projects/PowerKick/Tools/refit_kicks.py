"""Re-fits factory presets in place against their own reference kick with the tonality feature in the
distance (kick_features.py "tonal": spectral flatness of the mids 15..60 and 60..140 ms). Roman's listening
test (2026-10-23): many presets "sound like a synth, some like bells" - their mids ring as a note where real
kicks are noisy.

What stays: category, browser group, the note in the name (TUNE stays within the same semitone) and the
loudness (OUTPUT is set so the new peak equals the old one; kick levels are not re-normalised). Hand-given
names stay; a name generated from the sound ("Warm Clang F1 IV") is generated again when the sound changed.
A preset whose reference is itself more tonal than 90 % of its genre gets a new reference: the nearest one
of its genre that is at least as noisy as the genre's 25th percentile.

  python3 refit_kicks.py fit OUT.json NAME... | @FILE   re-fit the named presets (one name per line in FILE)
  python3 refit_kicks.py apply FIT.json                  write the fitted rows into Source/Presets.cpp

Env: PK_TESTS (render server), PK_REFS (default kick_features_v2.pkl), FIT_ITERS (120), FIT_POLISH (1),
FIT_WORKERS (9), PK_NUMPY_PATH,
  PK_START=FIT.json   start from (and keep the reference of) an earlier fit of the same presets;
  PK_GENRE_LIMITS=1   penalise mids more tonal than 90 % of the genre's references (60..140 ms) or ringing more
                      than 95 % of them (bands from ~350 Hz, 45..140 ms), with 1 dB to spare."""
import json, os, re, sys, time
from concurrent.futures import ProcessPoolExecutor

os.environ.setdefault("PK_REFS", "kick_features_v2.pkl")
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
if os.environ.get("PK_NUMPY_PATH"):
    sys.path.insert(0, os.environ["PK_NUMPY_PATH"])
import numpy as np
import fit_kicks as fk
from build_library import ORDER, INT, lit, name_of

GENERATED = re.compile(r"^(Wide )?(Clean|Warm|Clipped|Driven|Grinding|Crushed) (Rumble|Clang|Click|Knock|Drop|Boom|Punch|Thump) [A-G]#?\d( \S+)?$")
ROMAN = ["", " II", " III", " IV", " V", " VI", " VII", " VIII", " IX", " X", " XI", " XII"]

SRC = os.environ.get("PK_PRESETS_CPP", os.path.join(HERE, "..", "Source", "Presets.cpp"))
ROW = re.compile(r'^(\s*)\{ "([^"]+)",(\s*)"([^"]+)",(\s*)\{ ([^}]*) \}, "([^"]+)" \},(?: // fit ([\d.]+) to (.+?\.wav).*)?$')


def library():
    out = []
    for line in open(SRC, encoding="utf-8"):
        m = ROW.match(line.rstrip("\n"))
        if m:
            vals = [float(v.rstrip("f")) for v in m.group(6).split(",")]
            out.append({"name": m.group(2), "cat": m.group(4), "params": dict(zip(ORDER, vals)), "group": m.group(7),
                        "target": m.group(9)})
    return out


def _genre_limited(t10, g95):
    from kick_features import BAND_EDGES
    hi = BAND_EDGES[:-1] >= 300
    dist = fk.distance
    def distance(a, b, detail=False):
        res = dist(a, b, detail)
        pen = 2.0 * (max(0.0, t10 + 1.0 - float(a["tonal"][1])) + max(0.0, float(np.max(a["spec"][2][hi])) - (g95 - 1.0)))
        if detail:
            c, parts = res
            return c + pen, {**parts, "genre": pen}
        return res + pen
    fk.distance = distance


def genre_limits(refs, cat):
    from kick_features import BAND_EDGES
    hi = BAND_EDGES[:-1] >= 300
    g = [x["feat"] for x in refs.values() if x["category"] == cat]
    return (float(np.percentile([f["tonal"][1] for f in g], 10)), float(np.percentile([np.max(f["spec"][2][hi]) for f in g], 95)))


def work(job):
    name, target, start, others, seed, iters, limits = job
    if limits:
        _genre_limited(*limits)
    # The note in the name stays: TUNE within its semitone (TUNE 0 = A1); this worker runs one job at a time.
    note = re.search(r" ([A-G]#?)(\d)(?: \S+)?$", name) if GENERATED.match(name) else None
    if note:
        n = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"].index(note.group(1)) + 12 * (int(note.group(2)) + 1) - 33
    else:
        n = int(np.floor(start["tune"] + 0.5))
    fk.SPACE["tune"] = (n - 0.49, n + 0.49)
    fixed = {k: start[k] for k in ("rumbleSync",) if k in start}
    starts = [{**start, "tune": float(np.clip(start["tune"], n - 0.49, n + 0.49))}] + [{**o, "tune": float(np.clip(o["tune"], n - 0.49, n + 0.49))} for o in others]
    best, cost, parts = fk.fit_one((target, starts, seed, iters))
    best.update(fixed)
    # Loudness: the new peak equals the old one (both measured at the same OUTPUT).
    r = fk.Renderer()
    try:
        old_pk = r.feat({**start, "output": fk.FIT_OUTPUT})["peak_db"]
        new_pk = r.feat({**best, "output": fk.FIT_OUTPUT})["peak_db"]
        old_tonal = r.feat({**start, "output": fk.FIT_OUTPUT})["tonal"].tolist()
        new_tonal = r.feat({**best, "output": fk.FIT_OUTPUT})["tonal"].tolist()
    finally:
        r.p.kill()
    best["output"] = float(np.clip(start["output"] + old_pk - new_pk, -24.0, 6.0))
    return name, best, cost, parts, old_tonal, new_tonal


def fit(out_path, names):
    refs = {x["file"].split("/")[-1]: x for x in fk.load_refs()}
    lib = library()
    if os.environ.get("PK_START"):
        earlier = {f["name"]: f for f in json.load(open(os.environ["PK_START"], encoding="utf-8"))}
        for e in lib:
            if e["name"] in names and e["name"] in earlier:
                e["params"], e["target"], e["start_from"] = earlier[e["name"]]["params"], earlier[e["name"]]["target"], True
    sel = [e for e in lib if e["name"] in names]
    missing = names - {e["name"] for e in sel}
    if missing:
        raise SystemExit("not in the library: %s" % ", ".join(sorted(missing)))
    rng = np.random.default_rng(11)
    jobs = []
    r = fk.Renderer()
    for e in sel:
        if e["target"] not in refs:
            raise SystemExit("%s: its reference %s is not in %s" % (e["name"], e["target"], os.environ["PK_REFS"]))
        genre = [x for x in refs.values() if x["category"] == e["cat"]]
        tonal = np.array([x["feat"]["tonal"][1] for x in genre])
        if refs[e["target"]]["feat"]["tonal"][1] < np.percentile(tonal, 10) and not e.get("start_from"):
            mine = r.feat({**e["params"], "output": fk.FIT_OUTPUT})
            noisy = [x for x in genre if x["feat"]["tonal"][1] >= np.percentile(tonal, 25)]
            new = min(noisy, key=lambda x: fk.distance(mine, x["feat"]))["file"].split("/")[-1]
            print("%s: reference %s is too tonal for %s -> %s" % (e["name"], e["target"], e["cat"], new))
            e["target"] = new
    r.p.kill()
    for i, e in enumerate(sel):
        pool = [x["params"] for x in lib if x["cat"] == e["cat"] and x["name"] not in names]
        others = [pool[j] for j in rng.choice(len(pool), min(6, len(pool)), replace=False)] if pool else []
        jobs.append((e["name"], refs[e["target"]]["feat"], e["params"], others, 500 + i, int(os.environ.get("FIT_ITERS", 120)),
                     genre_limits(refs, e["cat"]) if os.environ.get("PK_GENRE_LIMITS") else None))
    os.environ.setdefault("FIT_POLISH", "1")
    # Distance of the current presets under the new measure, for the report.
    r = fk.Renderer()
    before = {e["name"]: fk.distance(r.feat({**e["params"], "output": fk.FIT_OUTPUT}), refs[e["target"]]["feat"]) for e in sel}
    r.p.kill()
    t0 = time.time()
    with ProcessPoolExecutor(max_workers=int(os.environ.get("FIT_WORKERS", 9))) as ex:
        results = list(ex.map(work, jobs, chunksize=1))
    out = []
    for e, (name, p, c, parts, ot, nt) in zip(sel, results):
        out.append({"name": name, "category": e["cat"], "group": e["group"], "target": e["target"], "params": p,
                    "distance": c, "before": before[name], "parts": parts, "tonal_before": ot, "tonal_after": nt})
        print("%-26s %-12s %5.1f -> %5.1f  tonal 60-140 %6.1f -> %6.1f dB" % (name, e["cat"], before[name], c, ot[1], nt[1]))
    json.dump(out, open(out_path, "w"), indent=1, ensure_ascii=False)
    print("%d presets in %.0f s" % (len(out), time.time() - t0))


def new_names(fits):
    """Old name -> name after the re-fit. Hand-given names stay. A generated name stays while it still describes the
    sound (same words); otherwise the words come from the new sound, the note from the old name (TUNE stayed within
    that semitone), and a numeral keeps names unique - also against the old names, so a project that still shows an
    old name never finds a different preset under it."""
    lib = {e["name"]: e for e in library()}
    out, renamed = {}, []
    for name, f in fits.items():
        m = GENERATED.match(name)
        if not m:
            out[name] = name
            continue
        note = re.search(r" ([A-G]#?\d)(?: \S+)?$", name).group(1)
        old_base = name[: name.rindex(note) + len(note)]
        new_words = name_of(f["params"]).rsplit(" ", 1)[0]
        base = "%s %s" % (new_words, note)
        if base == old_base:
            out[name] = name
        else:
            renamed.append((name, base))
    taken = set(lib) | set(out.values())  # a new name never reuses any preset's old name
    for name, base in renamed:
        i = 0
        while True:
            cand = base + (ROMAN[i] if i < len(ROMAN) else " %d" % (i + 1))
            if cand not in taken:
                break
            i += 1
        taken.add(cand)
        out[name] = cand
    return out


def apply(fit_path):
    fits = {f["name"]: f for f in json.load(open(fit_path, encoding="utf-8"))}
    names = new_names(fits)
    lines, n = [], 0
    for line in open(SRC, encoding="utf-8"):
        m = ROW.match(line.rstrip("\n"))
        if m and m.group(2) in fits:
            f = fits[m.group(2)]
            p = {**dict(zip(ORDER, [float(v.rstrip("f")) for v in m.group(6).split(",")])), **f["params"]}
            vals = ", ".join(lit(k, p[k]) for k in ORDER)
            nm = names[m.group(2)]
            pad = " " * max(1, len(m.group(2)) + len(m.group(3)) - len(nm))
            was = "" if nm == m.group(2) else ", was %s" % m.group(2)
            line = '%s{ "%s",%s"%s",%s{ %s }, "%s" }, // fit %.1f to %s (refit with tonality 2026-10-23%s)\n' % (
                m.group(1), nm, pad, m.group(4), m.group(5), vals, m.group(7), f["distance"], f["target"], was)
            n += 1
        lines.append(line)
    open(SRC, "w", encoding="utf-8").write("".join(lines))
    print("rewritten:", n, "of", len(fits))


if __name__ == "__main__":
    if sys.argv[1] == "fit":
        names = set()
        for a in sys.argv[3:]:
            names |= {l.strip() for l in open(a[1:], encoding="utf-8") if l.strip()} if a.startswith("@") else {a}
        fit(sys.argv[2], names)
    elif sys.argv[1] == "apply":
        apply(sys.argv[2])
