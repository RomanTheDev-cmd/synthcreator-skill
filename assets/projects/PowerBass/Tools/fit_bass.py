"""Matches PowerBass synthesis parameters to measured reference bass loops.

  python3 fit_bass.py audit          distance of each factory preset to the nearest reference of its category
  python3 fit_bass.py fit OUT.json   5 clusters per category -> one fitted preset per cluster medoid

Only feature numbers from the references are used; no reference audio ends up in PowerBass.
Master is fixed during fitting (density must come from synthesis, not output level) and set
afterwards so the riff peaks at -1 dBFS.
"""
import json, math, os, pickle, subprocess, sys, time
from concurrent.futures import ProcessPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
if os.environ.get("PK_NUMPY_PATH"):
    sys.path.insert(0, os.environ["PK_NUMPY_PATH"])
import numpy as np
from bass_features import features, distance

TESTS = os.environ.get("PB_TESTS", os.path.expanduser("~/Library/Caches/PowerBassBuild/powerbass_tests_artefacts/Release/powerbass_tests"))
SR, LEN, BPM = 48000, float(os.environ.get("PK_LEN", 3.0)), 150
FIT_MASTER, TARGET_PEAK = -12.0, -1.0

# name: (lo, hi, log-scale). splitFreq starts at 150 Hz: factory presets keep the sub mono (spec).
SPACE = {
    "shape": (0, 3, False), "sub": (0, 1, False), "oscDrive": (0, 1, False), "fmAmount": (0, 1, False),
    "fmDecay": (5, 3000, True), "oscMix": (0, 1, False), "cutoff": (40, 16000, True), "resonance": (0, 1, False),
    "envAmount": (-1, 1, False), "fltDecay": (10, 3000, True), "distDrive": (0, 1, False), "distTone": (0, 1, False),
    "distMix": (0, 1, False), "dark": (0, 1, False), "bite": (0, 1, False), "punch": (0, 1, False),
    "width": (0, 1, False), "splitFreq": (150, 1000, True), "attack": (0.5, 40, True), "decay": (20, 3000, True),
    "sustain": (0, 1, False), "release": (5, 400, True), "detune": (0, 1, False),
}
CHOICES = {"fmRatio": 18, "filterMode": 4, "distType": 5, "filterPos": 2}


def note_for(f0):
    n = int(round(69 + 12 * math.log2(max(f0, 20) / 440.0)))
    while n < 26: n += 12
    while n > 52: n -= 12
    return n


class Renderer:
    def __init__(self):
        self.p = subprocess.Popen([TESTS, "--serve"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    def render(self, params, root, pattern, bpm=BPM):
        params = {**params, "master": params.get("master", FIT_MASTER)}
        line = " ".join("%s=%g" % kv for kv in params.items()) + " _root=%d _bpm=%g _len=%g _pattern=%d\n" % (root, bpm, LEN, pattern)
        self.p.stdin.write(line.encode()); self.p.stdin.flush()
        n = int(LEN * SR) * 2 * 4
        buf = b""
        while len(buf) < n:
            buf += self.p.stdout.read(n - len(buf))
        return np.frombuffer(buf, dtype="<f4").reshape(-1, 2).astype(np.float64)

    def feat(self, params, root, pattern):
        return features(self.render(params, root, pattern), SR, f0=440.0 * 2 ** ((root - 69) / 12))


def load_refs():
    return pickle.load(open(os.path.join(HERE, "..", "Референсы", os.environ.get("PK_REFS", "bass_features.pkl")), "rb"))


def load_presets():
    out = subprocess.run([TESTS, "--dump-presets"], capture_output=True, text=True).stdout.strip().split("\n")
    res = []
    for line in out:
        name, cat, vals = line.split("|")
        res.append((name, cat, {k: float(v) for k, v in (t.split("=") for t in vals.split())}))
    return res


def setup(target):
    return note_for(target["f0"]), (1 if target["sustain"] > 0.6 else 0)


def audit():
    refs, r = load_refs(), Renderer()
    for c in sorted(set(x["category"] for x in refs)):
        fs = [x["feat"] for x in refs if x["category"] == c]
        nn = [min(distance(a, b) for j, b in enumerate(fs) if j != i) for i, a in enumerate(fs)]
        pairs = [distance(a, b) for i, a in enumerate(fs) for j, b in enumerate(fs) if j > i]
        print("  %-24s n=%3d  NN median %.1f  pair median %.1f" % (c, len(fs), np.median(nn), np.median(pairs)))
    for name, cat, vals in load_presets():
        pool = [x for x in refs if x["category"] == cat]
        if not pool:
            continue
        best = None
        for x in pool:
            root, pat = setup(x["feat"])
            d = distance(r.feat(vals, root, pat), x["feat"], True)
            if best is None or d[0] < best[0][0]:
                best = (d, x["file"])
        (tot, parts), f = best
        print("  %-18s %-24s %.1f  " % (name, cat, tot) + " ".join("%s %.1f" % kv for kv in parts.items()) + "  <- " + f.split("/")[-1])


def kmedoids(fs, k, iters=30, seed=1):
    n = len(fs)
    D = np.array([[distance(a, b) for b in fs] for a in fs]); D = (D + D.T) / 2
    rng = np.random.default_rng(seed)
    med = list(rng.choice(n, min(k, n), replace=False))
    for _ in range(iters):
        lab = np.argmin(D[:, med], axis=1)
        new = []
        for j in range(len(med)):
            m = np.where(lab == j)[0]
            new.append(med[j] if len(m) == 0 else int(m[np.argmin(D[np.ix_(m, m)].sum(axis=1))]))
        if new == med:
            break
        med = new
    return med, np.argmin(D[:, med], axis=1)


def to_unit(k, v):
    lo, hi, lg = SPACE[k]
    v = min(max(v, lo), hi)
    return (math.log(v) - math.log(lo)) / (math.log(hi) - math.log(lo)) if lg else (v - lo) / (hi - lo)


def from_unit(k, u):
    lo, hi, lg = SPACE[k]
    u = min(max(u, 0.0), 1.0)
    return math.exp(math.log(lo) + u * (math.log(hi) - math.log(lo))) if lg else lo + u * (hi - lo)


def mutate(p, sigma, rng):
    q = dict(p)
    for k in SPACE:
        if rng.random() < 0.4:
            q[k] = from_unit(k, to_unit(k, q[k]) + rng.normal(0, sigma))
    for k, n in CHOICES.items():
        if rng.random() < 0.07:
            q[k] = int(rng.integers(n))
    return q


DEFAULTS = {"shape": 2.0, "sub": 0.5, "oscDrive": 0.2, "fmAmount": 0.0, "fmDecay": 200, "oscMix": 0.0, "cutoff": 800,
            "resonance": 0.3, "envAmount": 0.4, "fltDecay": 250, "distDrive": 0.3, "distTone": 0.5, "distMix": 1.0,
            "dark": 0, "bite": 0, "punch": 0.3, "width": 0, "splitFreq": 300, "attack": 1, "decay": 300, "sustain": 0.8,
            "release": 80, "detune": 0.0, "fmRatio": 5, "filterMode": 0, "distType": 0, "filterPos": 0}


def fit_one(job):
    target, starts, seed, iters = job
    rng = np.random.default_rng(seed)
    r = Renderer()
    root, pat = setup(target)
    def cost(p):
        f = r.feat({**p, "master": FIT_MASTER}, root, pat)
        return (1e9, {}) if f is None else distance(f, target, True)
    best, bc, bparts = None, 1e9, {}
    for s in starts:
        s = {**DEFAULTS, **{k: v for k, v in s.items() if k in DEFAULTS}}
        c, parts = cost(s)
        if c < bc:
            best, bc, bparts = s, c, parts
    sigma = 0.15
    for _ in range(iters):
        improved = False
        for _ in range(8):
            q = mutate(best, sigma, rng)
            c, parts = cost(q)
            if c < bc:
                best, bc, bparts, improved = q, c, parts, True
        sigma = min(0.3, sigma * 1.25) if improved else max(0.01, sigma * 0.85)
    # Polish: coordinate grid over every parameter (escapes flat regions), then local refinement.
    for _ in range(int(os.environ.get("FIT_POLISH", 2))):
        for k in SPACE:
            for u in np.linspace(0, 1, 9):
                q = dict(best); q[k] = from_unit(k, float(u))
                c, parts = cost(q)
                if c < bc:
                    best, bc, bparts = q, c, parts
        for k, n in CHOICES.items():
            for v in range(n):
                q = dict(best); q[k] = v
                c, parts = cost(q)
                if c < bc:
                    best, bc, bparts = q, c, parts
        for _ in range(40):
            q = mutate(best, 0.05, rng)
            c, parts = cost(q)
            if c < bc:
                best, bc, bparts = q, c, parts
    x = r.render({**best, "master": FIT_MASTER}, root, pat)
    peak_db = 20 * math.log10(max(np.max(np.abs(x)), 1e-9))
    best["master"] = float(np.clip(FIT_MASTER + TARGET_PEAK - peak_db, -30, 6))
    r.p.kill()
    return best, bc, bparts


def fit(out_path, k):
    refs, presets = load_refs(), load_presets()
    jobs, meta = [], []
    for cat in sorted(set(x["category"] for x in refs)):
        pool = [x for x in refs if x["category"] == cat]
        med, lab = kmedoids([x["feat"] for x in pool], k)
        starts = [v for _, c, v in presets]
        for j, m in enumerate(med):
            jobs.append((pool[m]["feat"], starts, 500 + len(jobs), int(os.environ.get("FIT_ITERS", 250))))
            meta.append({"category": cat, "cluster_size": int(np.sum(lab == j)), "medoid": pool[m]["file"]})
    t0 = time.time()
    with ProcessPoolExecutor(max_workers=int(os.environ.get("FIT_WORKERS", 9))) as ex:
        results = list(ex.map(fit_one, jobs))
    for m, (p, c, parts) in zip(meta, results):
        m.update({"params": p, "distance": c, "parts": parts})
        print("%-24s cl %3d  %.2f  " % (m["category"], m["cluster_size"], c) + " ".join("%s %.1f" % kv for kv in parts.items()) + "  <- " + m["medoid"].split("/")[-1])
    json.dump(meta, open(out_path, "w"), indent=1, ensure_ascii=False)
    print("done in %.0f s" % (time.time() - t0))


def fit_extra(out_path, alloc, exclude_path):
    """More presets: k-medoids with k = alloc[category] over loops not already used as targets."""
    used = {m["medoid"] for m in json.load(open(exclude_path))}
    refs, presets = load_refs(), load_presets()
    starts = [v for _, c, v in presets]
    jobs, meta = [], []
    for cat, k in alloc.items():
        pool = [x for x in refs if x["category"] == cat and x["file"] not in used]
        k = min(k, len(pool))
        med, lab = kmedoids([x["feat"] for x in pool], k)
        for j, m in enumerate(med):
            jobs.append((pool[m]["feat"], starts, 3000 + len(jobs), int(os.environ.get("FIT_ITERS", 100))))
            meta.append({"category": cat, "cluster_size": int(np.sum(lab == j)), "medoid": pool[m]["file"]})
    t0 = time.time()
    with ProcessPoolExecutor(max_workers=int(os.environ.get("FIT_WORKERS", 9))) as ex:
        results = list(ex.map(fit_one, jobs, chunksize=1))
    for m, (p, c, parts) in zip(meta, results):
        m.update({"params": p, "distance": c, "parts": parts})
    json.dump(meta, open(out_path, "w"), indent=1, ensure_ascii=False)
    d = [m["distance"] for m in meta]
    print("%d extra presets in %.0f s, distance median %.1f, p90 %.1f" % (len(meta), time.time() - t0, np.median(d), np.percentile(d, 90)))


if __name__ == "__main__":
    if sys.argv[1] == "audit":
        audit()
    elif sys.argv[1] == "fit-extra":
        fit_extra(sys.argv[2], json.loads(os.environ["FIT_ALLOC"]), sys.argv[3])
    else:
        fit(sys.argv[2], int(os.environ.get("FIT_K", 5)))
