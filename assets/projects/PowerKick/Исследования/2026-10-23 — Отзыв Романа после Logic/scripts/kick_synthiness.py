"""«Синтовость» и «звон» кик-пресетов против референсов (признаки Tools/kick_features.py)."""
import json, os, pickle, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
TOOLS, BIN, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
sys.path.insert(0, os.path.expanduser("~/Library/Caches/PowerKickTools/py")); sys.path.insert(0, TOOLS)
import numpy as np
from kick_features import features, F0_TIMES, BAND_EDGES, ENV_FRAME
SR, LEN = 48000, 0.8
refs = pickle.load(open(os.path.join(TOOLS, "..", "Референсы", "kick_features.pkl"), "rb"))
late = (F0_TIMES >= 0.10) & (F0_TIMES <= 0.19); early = (F0_TIMES >= 0.008) & (F0_TIMES <= 0.016)
hi_bands = BAND_EDGES[:-1] >= 300  # полосы от ~350 Гц
def metrics(f):
    f0 = f["f0"]; body = np.nanmedian(f0[late]) if np.isfinite(f0[late]).sum() >= 3 else np.nan
    start = np.nanmedian(f0[early]) if np.isfinite(f0[early]).sum() >= 1 else np.nan
    env = f["env"]; below = np.where(env < -30)[0]; length = (below[0] if len(below) else len(env)) * ENV_FRAME * 1000
    ring_mid = float(np.max(f["spec"][2][hi_bands])); ring_tail = float(np.max(f["spec"][3][hi_bands]))
    return {"body_hz": float(55 * 2 ** (body / 12)) if np.isfinite(body) else np.nan, "drop_st": float(start - body) if np.isfinite(start) and np.isfinite(body) else np.nan,
            "len_ms": float(length), "ring45": ring_mid, "ring140": ring_tail}
R = {}
for r in refs:
    R.setdefault(r["category"], []).append(metrics(r["feat"]))
lines = subprocess.run([BIN, "--dump-presets"], capture_output=True, text=True, check=True).stdout.strip().split("\n")
items = []
for ln in lines:
    name, cat, vals = ln.split("|")[:3]; items.append((name, cat, {k: float(x) for k, x in (t.split("=") for t in vals.split())}))
def work(chunk):
    p = subprocess.Popen([BIN, "--serve"], stdin=subprocess.PIPE, stdout=subprocess.PIPE); res = []
    for name, cat, v in chunk:
        p.stdin.write((" ".join("%s=%g" % kv for kv in v.items()) + " _len=%g _sr=%d\n" % (LEN, SR)).encode()); p.stdin.flush()
        n = int(LEN * SR) * 8; buf = b""
        while len(buf) < n: buf += p.stdout.read(n - len(buf))
        x = np.frombuffer(buf, dtype="<f4").reshape(-1, 2).astype(np.float64)
        res.append((name, cat, metrics(features(x, SR)), v))
    p.kill(); return res
P = []
with ThreadPoolExecutor(10) as ex:
    for r in ex.map(work, [items[i::10] for i in range(10)]): P += r
json.dump([(n, c, m) for n, c, m, _ in P], open(os.path.join(OUT, "kick_synthiness.json"), "w"), ensure_ascii=False)
keys = [("body_hz", "тело, Гц"), ("drop_st", "падение питча, ст"), ("len_ms", "длина до −30 дБ, мс"), ("ring45", "звон 45–140 мс, дБ"), ("ring140", "звон 140–400 мс, дБ")]
for c in sorted(R):
    print("== %s: референсов %d, пресетов %d" % (c, len(R[c]), sum(1 for x in P if x[1] == c)))
    for k, lab in keys:
        rv = np.array([m[k] for m in R[c]], float); pv = np.array([m[k] for _, cc, m, _ in P if cc == c], float)
        q = lambda a, s: np.nanpercentile(a, s) if np.isfinite(a).any() else np.nan
        print("   %-22s реф. 10/50/90%%: %7.1f %7.1f %7.1f | пресеты: %7.1f %7.1f %7.1f" % (lab, q(rv, 10), q(rv, 50), q(rv, 90), q(pv, 10), q(pv, 50), q(pv, 90)))
# пресеты за пределами референсов своего жанра
print()
flags = []
for n, c, m, v in P:
    rc = R.get(c) or sum(R.values(), [])
    hi = {k: np.nanpercentile([x[k] for x in rc], 95) for k in ("body_hz", "ring45", "ring140", "len_ms")}
    lo_drop = np.nanpercentile([x["drop_st"] for x in rc], 5)
    why = []
    if m["body_hz"] > hi["body_hz"]: why.append("высокое тело %.0f Гц" % m["body_hz"])
    if np.isfinite(m["drop_st"]) and m["drop_st"] < lo_drop: why.append("мелкое падение питча %.0f ст" % m["drop_st"])
    if m["ring45"] > hi["ring45"]: why.append("звон 45–140 мс %.0f дБ" % m["ring45"])
    if m["ring140"] > hi["ring140"]: why.append("звон в хвосте %.0f дБ" % m["ring140"])
    if why: flags.append((n, c, why, v))
print("Пресетов вне 95%% референсов своего жанра: %d из %d" % (len(flags), len(P)))
from collections import Counter
print("  по признакам:", Counter(w.split(" ")[0] + " " + w.split(" ")[1] for _, _, ws, _ in flags for w in ws))
print("  по жанрам:", Counter(c for _, c, _, _ in flags))
ct = Counter(v.get("clickType", -1) for _, _, ws, v in flags if any("звон" in w for w in ws))
print("  тип щелчка у звенящих (0 Soft,1 Sharp,2 Metal,3 909,4 FM):", dict(ct), "| у всех пресетов:", dict(Counter(v.get("clickType", -1) for *_, v in P)))
for n, c, ws, v in flags[:60]: print("   %-24s %-12s %s" % (n, c, "; ".join(ws)))
