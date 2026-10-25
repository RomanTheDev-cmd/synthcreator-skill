"""Чистота тела (обертоны 112–632 Гц против основного 35–112 Гц в 140–400 мс) и доля Metal Click, пресеты против референсов."""
import json, os, pickle, subprocess, sys
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
TOOLS, BIN = sys.argv[1], sys.argv[2]
sys.path.insert(0, os.path.expanduser("~/Library/Caches/PowerKickTools/py")); sys.path.insert(0, TOOLS)
import numpy as np
from kick_features import features, BAND_EDGES
SR, LEN = 48000, 0.8
refs = pickle.load(open(os.path.join(TOOLS, "..", "Референсы", "kick_features.pkl"), "rb"))
def pw(db): return 10 ** (np.asarray(db) / 10)
def purity(spec):  # дБ: обертоны относительно основного тона в хвосте; чем ниже, тем «чище/синтовее»
    s = spec[3]; return float(10 * np.log10(pw(s[3:6]).sum() / (pw(s[1:3]).sum() + 1e-20) + 1e-20))
R = {}
for r in refs: R.setdefault(r["category"], []).append(purity(r["feat"]["spec"]))
lines = subprocess.run([BIN, "--dump-presets"], capture_output=True, text=True, check=True).stdout.strip().split("\n")
items = [(ln.split("|")[0], ln.split("|")[1], {k: float(x) for k, x in (t.split("=") for t in ln.split("|")[2].split())}) for ln in lines]
def work(chunk):
    p = subprocess.Popen([BIN, "--serve"], stdin=subprocess.PIPE, stdout=subprocess.PIPE); res = []
    for name, cat, v in chunk:
        p.stdin.write((" ".join("%s=%g" % kv for kv in v.items()) + " _len=%g _sr=%d\n" % (LEN, SR)).encode()); p.stdin.flush()
        n = int(LEN * SR) * 8; buf = b""
        while len(buf) < n: buf += p.stdout.read(n - len(buf))
        res.append((name, cat, purity(features(np.frombuffer(buf, dtype="<f4").reshape(-1, 2).astype(np.float64), SR)["spec"]), v))
    p.kill(); return res
P = []
with ThreadPoolExecutor(10) as ex:
    for r in ex.map(work, [items[i::10] for i in range(10)]): P += r
allref = sum(R.values(), [])
print("Обертоны тела в хвосте (дБ к основному тону; ниже = чище, «синтовее»):")
for c in sorted(R):
    pv = [x for n, cc, x, _ in P if cc == c]
    lo = np.percentile(R[c], 10)
    print("  %-12s реф. 10/50/90%%: %6.1f %6.1f %6.1f | пресеты: %6.1f %6.1f %6.1f | пресетов чище 10%% референсов: %d из %d" % (
        c, *np.percentile(R[c], (10, 50, 90)), *np.percentile(pv, (10, 50, 90)), sum(1 for x in pv if x < lo), len(pv)))
lo_all = {c: np.percentile(R.get(c, allref), 10) for c in set(x[1] for x in P)}
pure = [(x, n, c, v) for n, c, x, v in P if x < lo_all[c]]
print("Всего «чище» 10%% референсов своего жанра: %d из %d" % (len(pure), len(P)))
print("  их Drive Mode (0 Sat,1 Clip,2 Ind):", dict(Counter(v.get("driveMode") for *_, v in pure)), "Drive медиана %.2f против %.2f у всех" % (np.median([v.get("drive", 0) for *_, v in pure]), np.median([v.get("drive", 0) for *_, v in P])))
print("  примеры:", ", ".join("%s (%.0f)" % (n, x) for x, n, c, v in sorted(pure)[:25]))
print("Metal Click по жанрам:", {c: "%d/%d" % (sum(1 for n, cc, x, v in P if cc == c and v.get("clickType") == 2), sum(1 for n, cc, *_ in P if cc == c)) for c in sorted(set(x[1] for x in P))})
