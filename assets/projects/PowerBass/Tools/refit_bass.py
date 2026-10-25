"""Re-fits existing factory presets in place: names, categories and browser groups stay, the parameters are
fitted again to the reference loop of each preset, inside limits that keep presets usable in a rolling line
(Roman's listening test, 2026-10-23):
  RELEASE 5..150 ms (the tail is gone within a 1/16 kick gap), TONE >= 0.4, DARK <= 0.6, CUTOFF >= 150 Hz.
A preset whose reference is no longer in its category (Raw Analog sub layers, rumble loops in Rolling) gets a
new reference: k-medoids over the unused loops of that category.

  python3 refit_bass.py fit OUT.json [NAME ...]   # every fitted preset if no names are given
  python3 refit_bass.py apply FIT.json            # writes the parameters into Source/FactoryPresets.inc

Env: PB_TESTS (render server), PK_REFS (default bass_features_v3.pkl), FIT_ITERS (120), FIT_POLISH (1),
FIT_WORKERS (9), PB_LEVEL_CAP (off; e.g. 5: a sound that needs MASTER above +5 dB to reach -1 dBFS on the fitting
riff is penalised, for presets that otherwise end up below the level norm at the +6 dB maximum),
PB_NOTE_SPREAD (off; e.g. 8: a sound whose loudest of a 16th roll on C3 or G2 at 260 BPM and a legato riff on E2
peaks more than this many dB above a held C2 is penalised - such a resonant preset is normalised by its high notes and sounds quiet on the usual bass notes).
MASTER is set afterwards by normalize_peaks.py."""
import json, os, re, sys, time
from concurrent.futures import ProcessPoolExecutor

os.environ.setdefault("PK_REFS", "bass_features_v3.pkl")
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
if os.environ.get("PK_NUMPY_PATH"):
    sys.path.insert(0, os.environ["PK_NUMPY_PATH"])
import numpy as np
import fit_bass as fb

# The limits apply in every worker process: they import this module and the patch below runs there too.
fb.SPACE.update({"release": (5, 150, True), "distTone": (0.4, 1, False), "dark": (0, 0.6, False),
                 "cutoff": (150, 16000, True)})
INC = os.environ.get("PB_INC", os.path.join(HERE, "..", "Source", "FactoryPresets.inc"))
ENTRY = re.compile(r'^\{ "([^"]+)", "([^"]+)", \{ (.*) \}, "([^"]+)" \},(?: // fit [\d.]+ to (.+?)(?: \(.*\))?)?$')


def library(path=None):
    out = []
    for line in open(path or INC, encoding="utf-8"):
        m = ENTRY.match(line.rstrip("\n"))
        if m:
            vals = {k: float(v.rstrip("f")) for k, v in re.findall(r'\{"([^"]+)",(-?[\d.e+]+f?)\}', m.group(3))}
            out.append({"name": m.group(1), "cat": m.group(2), "params": vals, "group": m.group(4), "target": m.group(5)})
    return out


def clamp(p):
    q = dict(p)
    for k in fb.SPACE:
        if k in q:
            q[k] = fb.from_unit(k, fb.to_unit(k, q[k]))
    return q


def _level_capped():
    """Adds the riff peak to the features and a penalty to the distance when MASTER would exceed PB_LEVEL_CAP."""
    cap = float(os.environ["PB_LEVEL_CAP"])
    feat, dist = fb.Renderer.feat, fb.distance
    def feat_with_peak(self, params, root, pattern):
        x = self.render(params, root, pattern)
        f = fb.features(x, fb.SR, f0=440.0 * 2 ** ((root - 69) / 12))
        if f is not None:
            f["peak_db"] = 20 * np.log10(max(np.max(np.abs(x)), 1e-9))
        return f
    def distance(a, b, detail=False):
        res = dist(a, b, detail)
        need = fb.FIT_MASTER + fb.TARGET_PEAK - a.get("peak_db", 0.0) if isinstance(a, dict) else 0.0
        pen = 2.0 * max(0.0, need - cap)
        if detail:
            c, parts = res
            return c + pen, {**parts, "level": pen}
        return res + pen
    fb.Renderer.feat, fb.distance = feat_with_peak, distance


def _note_spread_limited():
    cap = float(os.environ["PB_NOTE_SPREAD"])
    feat, dist = fb.Renderer.feat, fb.distance
    def feat_with_spread(self, params, root, pattern):
        f = feat(self, params, root, pattern)
        if f is not None:
            pk = lambda x: 20 * np.log10(max(np.max(np.abs(x)), 1e-9))
            loud = max(pk(self.render(params, root_, pat, bpm)) for root_, pat, bpm in ((48, 0, 260), (43, 0, 260), (40, 1, 150)))
            f["note_spread"] = loud - pk(self.render(params, 36, 2, 150))
        return f
    def distance(a, b, detail=False):
        res = dist(a, b, detail)
        pen = 2.0 * max(0.0, a.get("note_spread", 0.0) - cap) if isinstance(a, dict) else 0.0
        if detail:
            c, parts = res
            return c + pen, {**parts, "spread": pen}
        return res + pen
    fb.Renderer.feat, fb.distance = feat_with_spread, distance


def work(job):
    target, start, others, seed, iters = job
    if os.environ.get("PB_LEVEL_CAP"):  # first: it replaces the feature function, the spread wraps it
        _level_capped()
    if os.environ.get("PB_NOTE_SPREAD"):
        _note_spread_limited()
    best, cost, parts = fb.fit_one((target, [clamp(start)] + [clamp(o) for o in others], seed, iters))
    return clamp(best), cost, parts


def fit(out_path, names):
    refs = fb.load_refs()
    lib = [e for e in library() if e["cat"] != "Deep Sub Bass" and (not names or e["name"] in names)]
    pool = {}
    for r in refs:
        pool.setdefault(r["category"], {})[r["file"].split("/")[-1]] = r
    # Presets whose reference left the category get new, distinct references.
    for cat in sorted({e["cat"] for e in lib}):
        es = [e for e in lib if e["cat"] == cat]
        lost = [e for e in es if e["target"] not in pool.get(cat, {})]
        if lost:
            used = {e["target"] for e in es if e not in lost}
            free = [r for k, r in pool[cat].items() if k not in used]
            if len(free) < len(lost):  # fewer loops than presets (Rolling: 67 loops, 100 presets): share them
                free = list(pool[cat].values())
            med, _ = fb.kmedoids([r["feat"] for r in free], min(len(lost), len(free)))
            for j, e in enumerate(lost):
                e["target"] = free[med[j % len(med)]]["file"].split("/")[-1]
    rng = np.random.default_rng(7)
    everyone = [e["params"] for e in library()]
    jobs = [(pool[e["cat"]][e["target"]]["feat"], e["params"], [everyone[i] for i in rng.choice(len(everyone), 12, replace=False)],
             900 + n, int(os.environ.get("FIT_ITERS", 120))) for n, e in enumerate(lib)]
    os.environ.setdefault("FIT_POLISH", "1")
    t0 = time.time()
    with ProcessPoolExecutor(max_workers=int(os.environ.get("FIT_WORKERS", 9))) as ex:
        results = list(ex.map(work, jobs, chunksize=1))
    out = []
    for e, (p, c, parts) in zip(lib, results):
        out.append({"name": e["name"], "category": e["cat"], "group": e["group"], "target": e["target"], "params": p,
                    "distance": c, "parts": parts})
        print("%-22s %-24s %.2f  <- %s" % (e["name"], e["cat"], c, e["target"]))
    json.dump(out, open(out_path, "w"), indent=1, ensure_ascii=False)
    print("%d presets in %.0f s" % (len(out), time.time() - t0))


def lit(k, v):
    if k in {"fmRatio", "filterMode", "distType", "filterPos", "glideMode", "oversample"}:
        return str(int(round(v)))
    t = "%.4g" % v
    return (t + ".0" if "." not in t and "e" not in t else t) + "f"


def apply(fit_path):
    fits = {f["name"]: f for f in json.load(open(fit_path))}
    lines, n = [], 0
    for line in open(INC, encoding="utf-8"):
        m = ENTRY.match(line.rstrip("\n"))
        if m and m.group(1) in fits:
            f = fits[m.group(1)]
            old = {k: float(v.rstrip("f")) for k, v in re.findall(r'\{"([^"]+)",(-?[\d.e+]+f?)\}', m.group(3))}
            p = {k: v for k, v in f["params"].items() if k != "master"}
            p["master"] = old.get("master", -12.0)  # normalize_peaks.py sets it next
            body = ",".join('{"%s",%s}' % (k, lit(k, p[k])) for k in sorted(p))
            line = '{ "%s", "%s", { %s }, "%s" }, // fit %.1f to %s (refit 2026-10-23)\n' % (
                m.group(1), m.group(2), body, m.group(4), f["distance"], f["target"])
            n += 1
        lines.append(line)
    open(INC, "w", encoding="utf-8").write("".join(lines))
    print("rewritten:", n, "of", len(fits))


if __name__ == "__main__":
    if sys.argv[1] == "fit":
        names = set()
        for a in sys.argv[3:]:  # "@FILE" reads one preset name per line
            names |= {l.strip() for l in open(a[1:], encoding="utf-8") if l.strip()} if a.startswith("@") else {a}
        fit(sys.argv[2], names)
    elif sys.argv[1] == "apply":
        apply(sys.argv[2])
