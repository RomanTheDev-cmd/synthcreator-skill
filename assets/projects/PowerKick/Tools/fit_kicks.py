"""Matches PowerKick's synthesis parameters to measured reference kicks.

  python3 fit_kicks.py audit            how far each factory preset is from real kicks
  python3 fit_kicks.py fit OUT.json     cluster references per category, fit one preset per cluster medoid

Only feature numbers from the references are used; no reference audio ends up in PowerKick.
"""
import json, os, pickle, subprocess, sys, time
from concurrent.futures import ProcessPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
if os.environ.get("PK_NUMPY_PATH"):
    sys.path.insert(0, os.environ["PK_NUMPY_PATH"])
import numpy as np
from kick_features import features, distance

TESTS = os.environ.get("PK_TESTS", os.path.expanduser("~/Library/Caches/PowerKickBuild/powerkick_tests_artefacts/Release/powerkick_tests"))
SR, LEN, BPM = 48000, 0.6, 145
FIT_OUTPUT = -12.0     # fixed during fitting: density must come from synthesis, never from output clipping
TARGET_PEAK = -1.0     # presets ship with ~1 dB headroom for the mix

# Continuous search space (plain units) and discrete choices.
SPACE = {
    "tune": (-9, 9), "decay": (0.05, 0.9), "punch": (0, 1), "body": (0, 1), "click": (0, 1), "drive": (0, 1),
    "character": (0, 1), "rumble": (0, 1), "pitchDepth": (6, 48), "pitchTime": (4, 120),
    "clickTone": (0, 1), "rumbleTime": (0, 1), "width": (0, 100), "bodyTone": (0, 1), "pitchFall": (0, 24),
}
CHOICES = {"driveMode": 3, "charMode": 5, "clickType": 3}


class Renderer:
    def __init__(self):
        self.p = subprocess.Popen([TESTS, "--serve"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    def render(self, params):
        params = {**params, "output": params.get("output", FIT_OUTPUT)}
        line = " ".join("%s=%g" % kv for kv in params.items()) + " _len=%g _sr=%d\n" % (LEN, SR)
        self.p.stdin.write(line.encode())
        self.p.stdin.flush()
        n = int(LEN * SR) * 2 * 4
        buf = b""
        while len(buf) < n:
            buf += self.p.stdout.read(n - len(buf))
        return np.frombuffer(buf, dtype="<f4").reshape(-1, 2).astype(np.float64)

    def feat(self, params):
        return features(self.render(params), SR)


def load_refs():
    return pickle.load(open(os.path.join(HERE, "..", "Референсы", os.environ.get("PK_REFS", "kick_features.pkl")), "rb"))


def load_presets():
    out = subprocess.run([TESTS, "--dump-presets"], capture_output=True, text=True).stdout.strip().split("\n")
    presets = []
    for line in out:
        name, cat, vals = line.split("|")
        presets.append((name, cat, {k: float(v) for k, v in (t.split("=") for t in vals.split())}))
    return presets


def audit():
    refs = load_refs()
    r = Renderer()
    cats = sorted(set(x["category"] for x in refs))
    print("Reference spread (distance units): nearest-neighbour / typical pair, per category")
    for c in cats:
        fs = [x["feat"] for x in refs if x["category"] == c]
        nn, pairs = [], []
        for i, a in enumerate(fs):
            d = [distance(a, b) for j, b in enumerate(fs) if j != i]
            nn.append(min(d)); pairs += d
        print("  %-12s n=%3d  NN median %.1f   pair median %.1f" % (c, len(fs), np.median(nn), np.median(pairs)))
    print("\nPreset -> nearest reference in its category (and its breakdown)")
    for name, cat, vals in load_presets():
        f = r.feat(vals)
        pool = [x for x in refs if x["category"] == cat] or refs
        ds = [distance(f, x["feat"], True) for x in pool]
        best = min(range(len(ds)), key=lambda i: ds[i][0])
        tot, parts = ds[best]
        print("  %-18s %-17s %.1f  env %.1f f0 %.1f spec %.1f side %.1f   <- %s" % (
            name, cat, tot, parts["env"], parts["f0"], parts["spec"], parts["side"], pool[best]["file"].split("/")[-1]))


#==============================================================================
def kmedoids(fs, k, iters=30, seed=1):
    n = len(fs)
    D = np.array([[distance(a, b) for b in fs] for a in fs])
    D = (D + D.T) / 2
    rng = np.random.default_rng(seed)
    med = list(rng.choice(n, k, replace=False))
    for _ in range(iters):
        lab = np.argmin(D[:, med], axis=1)
        new = []
        for j in range(k):
            members = np.where(lab == j)[0]
            if len(members) == 0:
                new.append(med[j]); continue
            new.append(int(members[np.argmin(D[np.ix_(members, members)].sum(axis=1))]))
        if new == med:
            break
        med = new
    lab = np.argmin(D[:, med], axis=1)
    return med, lab, D


def random_params(rng):
    p = {k: rng.uniform(lo, hi) for k, (lo, hi) in SPACE.items()}
    p.update({k: int(rng.integers(n)) for k, n in CHOICES.items()})
    return p


def mutate(p, sigma, rng):
    q = dict(p)
    for k, (lo, hi) in SPACE.items():
        if rng.random() < 0.5:
            q[k] = float(np.clip(q[k] + rng.normal(0, sigma * (hi - lo)), lo, hi))
    for k, n in CHOICES.items():
        if rng.random() < 0.08:
            q[k] = int(rng.integers(n))
    return q


def fit_one(job):
    """(1+8)-ES with restarts from the best existing presets. Returns params, distance, parts."""
    target, starts, seed, iters = job
    rng = np.random.default_rng(seed)
    r = Renderer()
    def cost(p):
        f = r.feat({**p, "output": FIT_OUTPUT})
        return (1e9, {}) if f is None else distance(f, target, True)
    best, (bc, bparts) = None, (1e9, {})
    defaults = {"bodyTone": 1.0, "pitchFall": 0.0}
    starts = [{**defaults, **s} for s in starts]
    for s in starts + [random_params(rng) for _ in range(4)]:
        c, parts = cost(s)
        if c < bc:
            best, bc, bparts = dict(s), c, parts
    sigma = 0.15
    for it in range(iters):
        improved = False
        for _ in range(8):
            q = mutate(best, sigma, rng)
            c, parts = cost(q)
            if c < bc:
                best, bc, bparts, improved = q, c, parts, True
        sigma = min(0.3, sigma * 1.25) if improved else max(0.01, sigma * 0.85)
    # Polish: coordinate grid search over the whole range of every parameter, so flat regions
    # (e.g. bodyTone between 0.7 and 1, where nothing changes) cannot trap the search.
    for _ in range(int(os.environ.get("FIT_POLISH", 3))):
        for k, (lo, hi) in SPACE.items():
            for v in np.linspace(lo, hi, 11):
                q = dict(best); q[k] = float(v)
                c, parts = cost(q)
                if c < bc:
                    best, bc, bparts = q, c, parts
        for k, n in CHOICES.items():
            for v in range(n):
                q = dict(best); q[k] = v
                c, parts = cost(q)
                if c < bc:
                    best, bc, bparts = q, c, parts
        # local refinement around the polished point
        sigma = 0.05
        for _ in range(60):
            q = mutate(best, sigma, rng)
            c, parts = cost(q)
            if c < bc:
                best, bc, bparts = q, c, parts
    # Ship level: peak at TARGET_PEAK.
    f = r.feat({**best, "output": FIT_OUTPUT})
    best["output"] = float(np.clip(FIT_OUTPUT + TARGET_PEAK - f["peak_db"], -24, 6))
    r.p.kill()
    return best, bc, bparts


def fit(out_path, per_cat):
    refs = load_refs()
    presets = load_presets()
    jobs, meta = [], []
    for cat, k in per_cat.items():
        pool = [x for x in refs if x["category"] == cat]
        med, lab, D = kmedoids([x["feat"] for x in pool], k)
        starts = [v for _, c, v in presets]  # every factory preset is a candidate start
        for j, m in enumerate(med):
            members = int(np.sum(lab == j))
            jobs.append((pool[m]["feat"], [dict(s) for s in starts], 100 + len(jobs), int(os.environ.get("FIT_ITERS", 300))))
            meta.append({"category": cat, "cluster_size": members, "medoid": pool[m]["file"]})
    t0 = time.time()
    with ProcessPoolExecutor(max_workers=int(os.environ.get("FIT_WORKERS", 9))) as ex:
        results = list(ex.map(fit_one, jobs))
    for m, (p, c, parts) in zip(meta, results):
        m.update({"params": p, "distance": c, "parts": parts})
        print("%-12s cluster %3d  dist %.2f  env %.1f f0 %.1f spec %.1f side %.1f  <- %s" % (
            m["category"], m["cluster_size"], c, parts["env"], parts["f0"], parts["spec"], parts["side"], m["medoid"].split("/")[-1]))
    json.dump(meta, open(out_path, "w"), indent=1, ensure_ascii=False)
    print("done in %.0f s" % (time.time() - t0))


def fit_extra(out_path, alloc, exclude_path):
    """More presets: k-medoids with k = alloc[category] over references not already used as
    targets by the curated set (exclude_path = an earlier fit JSON)."""
    used = {m["medoid"] for m in json.load(open(exclude_path))}
    refs, presets = load_refs(), load_presets()
    starts = [v for _, c, v in presets]
    jobs, meta = [], []
    for cat, k in alloc.items():
        pool = [x for x in refs if x["category"] == cat and x["file"] not in used]
        k = min(k, len(pool))
        med, lab, D = kmedoids([x["feat"] for x in pool], k)
        for j, m in enumerate(med):
            jobs.append((pool[m]["feat"], [dict(s) for s in starts], 1000 + len(jobs), int(os.environ.get("FIT_ITERS", 250))))
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
    elif sys.argv[1] == "fit":
        fit(sys.argv[2], json.loads(os.environ.get("FIT_PER_CAT", '{"Techno": 6, "Hard Techno": 6, "Schranz": 6, "Industrial": 4}')))
