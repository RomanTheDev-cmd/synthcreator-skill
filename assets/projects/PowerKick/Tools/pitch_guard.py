"""Keeps factory kicks in the kick register (no "lead"-like presets).

A kick that stays high (tail above ~E2) or glides down slowly from far above reads as a pitched
synth note. Limits come from the measured references (Hard Techno / Schranz / Techno, p90-p97):

  window       reference p90-p97     limit
  30-60 ms     151-167 Hz            170 Hz
  60-120 ms    94-101 Hz             100 Hz
  120-200 ms   70-90 Hz              80 Hz

plus the share of energy above 200 Hz in the body (45-140 ms / 140-400 ms), which is how
strongly the tone itself is heard over the low end.

  python3 pitch_guard.py audit                       lists factory presets outside the limits
  python3 pitch_guard.py refit OUT.json NAME...      refits those presets with the limits as a
                                                     penalty (target = their reference, or the nearest
                                                     in-range reference of the category when the
                                                     original reference is itself out of range)
  python3 pitch_guard.py refit-all OUT.json          refits every reference-fitted preset (after an
                                                     engine change) with the same limits
  python3 pitch_guard.py apply OUT.json              writes the refits into Source/Presets.cpp (new
                                                     name from the sound: the note changed), then
                                                     rebuild and run normalize_peaks.py
"""
import json, os, re, sys, time
from concurrent.futures import ProcessPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
if os.environ.get("PK_NUMPY_PATH"):
    sys.path.insert(0, os.environ["PK_NUMPY_PATH"])
import numpy as np
import fit_kicks as fk
from kick_features import F0_TIMES, distance

WINDOWS = [(0.03, 0.06), (0.06, 0.12), (0.12, 0.20)]
CAP_HZ = [170.0, 100.0, 80.0]
CAP_ST = [12 * np.log2(h / 55.0) for h in CAP_HZ]
# Upper-band share caps: Techno / Hard Techno / Schranz near the Hard Techno reference p90;
# Industrial references are noisier (grind, not tone), and gabba / rawstyle are distorted by
# genre, so they get more room.
SHARE_CAP = {"Industrial": (0.15, 0.10), "Rave / Hard Dance": (0.15, 0.10)}
SHARE_DEFAULT = (0.07, 0.05)


def pitch_windows(f0):
    out = []
    for lo, hi in WINDOWS:
        m = (F0_TIMES >= lo) & (F0_TIMES < hi) & ~np.isnan(f0)
        out.append(float(np.median(f0[m])) if np.any(m) else np.nan)
    return out


def upper_share(spec, seg):
    p = 10 ** (spec[seg] / 10)
    return float(p[4:].sum() / p.sum())  # bands from 200 Hz up


def excess(feat, category):
    """0 inside the limits; grows with semitones above the pitch caps and with the upper share."""
    w = pitch_windows(feat["f0"])
    e = sum(max(0.0, v - c) for v, c in zip(w, CAP_ST) if not np.isnan(v))
    caps = SHARE_CAP.get(category, SHARE_DEFAULT)
    for seg, cap in zip((2, 3), caps):
        s = upper_share(feat["spec"], seg)
        if s > cap:
            e += 20.0 * (s - cap)
    return e


def describe(feat, category):
    w = pitch_windows(feat["f0"])
    hz = ["%.0f" % (55 * 2 ** (v / 12)) if not np.isnan(v) else "-" for v in w]
    return "f0 %s Hz, upper %.3f/%.3f" % ("/".join(hz), upper_share(feat["spec"], 2), upper_share(feat["spec"], 3))


def audit():
    r = fk.Renderer()
    bad = 0
    for name, cat, v in fk.load_presets():
        f = r.feat(v)
        e = excess(f, cat)
        if e > 0:
            bad += 1
            print("  %-28s %-18s excess %.2f   %s" % (name, cat, e, describe(f, cat)))
    print("%d presets outside the kick register" % bad)


def _job(job):
    target, category, starts, seed, iters = job
    rng = np.random.default_rng(seed)
    r = fk.Renderer()
    space = dict(fk.SPACE)
    space["tune"] = (-9, 3)  # settled pitch at most C2

    def cost(p):
        f = r.feat({**p, "output": fk.FIT_OUTPUT})
        if f is None:
            return 1e9, 0.0, 0.0
        d = distance(f, target)
        e = excess(f, category)
        return d + 6.0 * e, d, e

    def clip(p):
        q = dict(p)
        for k, (lo, hi) in space.items():
            q[k] = float(np.clip(q[k], lo, hi))
        return q

    def mutate(p, sigma):
        q = dict(p)
        for k, (lo, hi) in space.items():
            if rng.random() < 0.5:
                q[k] = float(np.clip(q[k] + rng.normal(0, sigma * (hi - lo)), lo, hi))
        for k, n in fk.CHOICES.items():
            if rng.random() < 0.08:
                q[k] = int(rng.integers(n))
        return q

    best, bc = None, (1e9, 0, 0)
    for s in starts:
        s = clip({"bodyTone": 1.0, "pitchFall": 0.0, **s})
        c = cost(s)
        if c[0] < bc[0]:
            best, bc = s, c
    sigma = 0.15
    for _ in range(iters):
        improved = False
        for _ in range(8):
            q = mutate(best, sigma)
            c = cost(q)
            if c[0] < bc[0]:
                best, bc, improved = q, c, True
        sigma = min(0.3, sigma * 1.25) if improved else max(0.01, sigma * 0.85)
    for _ in range(2):
        for k, (lo, hi) in space.items():
            for v in np.linspace(lo, hi, 11):
                q = dict(best); q[k] = float(v)
                c = cost(q)
                if c[0] < bc[0]:
                    best, bc = q, c
        for k, n in fk.CHOICES.items():
            for v in range(n):
                q = dict(best); q[k] = v
                c = cost(q)
                if c[0] < bc[0]:
                    best, bc = q, c
        for _ in range(60):
            q = mutate(best, 0.05)
            c = cost(q)
            if c[0] < bc[0]:
                best, bc = q, c
    f = r.feat({**best, "output": fk.FIT_OUTPUT})
    best["output"] = float(np.clip(fk.FIT_OUTPUT + fk.TARGET_PEAK - f["peak_db"], -24, 6))
    r.p.kill()
    return best, bc[1], bc[2]


def refit(out_path, names):
    refs = fk.load_refs()
    by_base = {}
    for x in refs:
        by_base.setdefault(x["file"].split("/")[-1], []).append(x)
    src = open(os.path.join(HERE, "..", "Source", "Presets.cpp")).read()
    comments = dict(re.findall(r'\{ "([^"]+)",[^\n]*// fit [0-9.]+ to ([^\n]+)', src))
    used = set(comments.values())
    presets = fk.load_presets()
    starts = [v for _, _, v in presets]
    cats = {n: c for n, c, _ in presets}
    jobs, meta = [], []
    for name in names:
        cat = cats[name]
        orig = by_base.get(comments.get(name, ""), [None])[0]
        pool = [x for x in refs if x["category"] == cat]
        if orig is not None and excess(orig["feat"], cat) == 0:
            target = orig
        else:
            ok = [x for x in pool if excess(x["feat"], cat) == 0 and x["file"].split("/")[-1] not in used]
            target = min(ok, key=lambda x: distance(x["feat"], orig["feat"])) if orig is not None else ok[0]
        used.add(target["file"].split("/")[-1])
        jobs.append((target["feat"], cat, [dict(s) for s in starts], 5000 + len(jobs), int(os.environ.get("FIT_ITERS", 200))))
        meta.append({"name": name, "category": cat, "medoid": target["file"],
                     "original": orig["file"] if orig is not None else None})
        print("%-28s -> %s%s" % (name, target["file"].split("/")[-1], "" if orig is target else "  (original out of range)"), flush=True)
    t0 = time.time()
    with ProcessPoolExecutor(max_workers=int(os.environ.get("FIT_WORKERS", 9))) as ex:
        res = list(ex.map(_job, jobs, chunksize=1))
    for m, (p, d, e) in zip(meta, res):
        m.update({"params": p, "distance": d, "excess": e})
        print("%-28s dist %.1f excess %.2f" % (m["name"], d, e))
    json.dump(meta, open(out_path, "w"), indent=1, ensure_ascii=False)
    print("done in %.0f s" % (time.time() - t0))


def refit_all(out_path):
    """Refits every reference-fitted preset (after an engine change), keeping the kick-register
    limits. Starts: the preset itself plus 60 other presets."""
    refs = fk.load_refs()
    by_base = {}
    for x in refs:
        by_base.setdefault(x["file"].split("/")[-1], []).append(x)
    src = open(os.path.join(HERE, "..", "Source", "Presets.cpp")).read()
    comments = dict(re.findall(r'\{ "([^"]+)",[^\n]*// fit [0-9.]+ to ([^\n(]+?)(?: \(kick register\))?\s*$', src, re.M))
    presets = fk.load_presets()
    own = {n: v for n, _, v in presets}
    names = [n for n, _, _ in presets if comments.get(n, "") in by_base]
    used = {comments[n] for n in names}
    rng = np.random.default_rng(2026)
    jobs, meta = [], []
    for name in names:
        cat = next(c for n, c, _ in presets if n == name)
        orig = by_base[comments[name]][0]
        target = orig
        if excess(orig["feat"], cat) > 0:
            ok = [x for x in refs if x["category"] == cat and excess(x["feat"], cat) == 0 and x["file"].split("/")[-1] not in used]
            target = min(ok, key=lambda x: distance(x["feat"], orig["feat"]))
            used.add(target["file"].split("/")[-1])
        others = [own[n] for n in rng.choice(list(own), 60, replace=False)]
        jobs.append((target["feat"], cat, [dict(own[name])] + [dict(o) for o in others], 9000 + len(jobs),
                     int(os.environ.get("FIT_ITERS", 250))))
        meta.append({"name": name, "category": cat, "medoid": target["file"], "original": orig["file"]})
    # Every finished preset goes straight to OUT.jsonl: an interrupted run resumes where it stopped.
    from concurrent.futures import as_completed
    partial = out_path + "l"
    done = {}
    if os.path.exists(partial):
        for line in open(partial):
            if line.strip():
                m = json.loads(line)
                done[m["name"]] = m
    todo = [i for i, m in enumerate(meta) if m["name"] not in done]
    print("refitting %d presets (%d already done)" % (len(todo), len(done)), flush=True)
    t0 = time.time()
    with ProcessPoolExecutor(max_workers=int(os.environ.get("FIT_WORKERS", 9))) as ex, open(partial, "a") as log:
        futures = {ex.submit(_job, jobs[i]): i for i in todo}
        for k, fut in enumerate(as_completed(futures), 1):
            m = dict(meta[futures[fut]])
            p, d, e = fut.result()
            m.update({"params": p, "distance": d, "excess": e})
            log.write(json.dumps(m, ensure_ascii=False) + "\n")
            log.flush()
            done[m["name"]] = m
            print("%3d/%d %-28s dist %.1f excess %.2f" % (k, len(todo), m["name"], d, e), flush=True)
    meta = [done[m["name"]] for m in meta]
    json.dump(meta, open(out_path, "w"), indent=1, ensure_ascii=False)
    d = [m["distance"] for m in meta]
    print("done in %.0f s: distance median %.2f, p90 %.2f, max excess %.2f" % (time.time() - t0, np.median(d), np.percentile(d, 90), max(m["excess"] for m in meta)))


def apply(fit_json):
    from build_library import ORDER, group_of, lit, name_of
    path = os.path.join(HERE, "..", "Source", "Presets.cpp")
    src = open(path).read()
    names = set(re.findall(r'^    \{ "([^"]+)",', src, re.M))
    for m in json.load(open(fit_json)):
        p = dict(m["params"])
        p.setdefault("delay", 0.0); p.setdefault("rumbleSync", 0); p.setdefault("splitFreq", 500.0)
        if p.get("width", 0) < 3:
            p["width"], p["delay"] = 0.0, 0.0
        names.discard(m["name"])
        old_base = re.sub(r" (?:[IVX]+|\d+)$", "", m["name"])
        if not re.search(r" [A-G]#?-?\d$", old_base) or old_base == name_of(p):
            n = m["name"]  # hand-given name, or the descriptive name still fits the sound: keep it
        else:
            base = n = name_of(p)
            k = 2
            roman = ["II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII"]
            while n in names:  # II..XII, then plain numbers (never loops)
                n = "%s %s" % (base, roman[k - 2] if k - 2 < len(roman) else str(k))
                k += 1
        names.add(n)
        row = '    { %-28s %-20s { %s }, "%s" }, // fit %.1f to %s (kick register)' % (
            '"%s",' % n, '"%s",' % m["category"], ", ".join(lit(k, p[k]) for k in ORDER), group_of(p),
            m["distance"], m["medoid"].split("/")[-1])
        line = re.search(r'^    \{ "%s",[^\n]*$' % re.escape(m["name"]), src, re.M)
        src = src[:line.start()] + row + src[line.end():]
        print("%-28s -> %s" % (m["name"], n))
    open(path, "w").write(src)


if __name__ == "__main__":
    if sys.argv[1] == "audit":
        audit()
    elif sys.argv[1] == "refit":
        refit(sys.argv[2], sys.argv[3:])
    elif sys.argv[1] == "apply":
        apply(sys.argv[2])
    elif sys.argv[1] == "refit-all":
        refit_all(sys.argv[2])
