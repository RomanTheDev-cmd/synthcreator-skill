"""Яркость бас-пресетов против референсов того же жанра (признаки harm из Tools/bass_features.py)."""
import json, math, os, pickle, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
TOOLS, BIN, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
sys.path.insert(0, os.path.expanduser("~/Library/Caches/PowerKickTools/py")); sys.path.insert(0, TOOLS)
import numpy as np
from bass_features import features
SR, LEN = 48000, 3.0
refs = pickle.load(open(os.path.join(TOOLS, "..", "Референсы", "bass_features_v2.pkl"), "rb"))
def bright(h):  # доли энергии выше 6f и выше 24f, дБ к общей
    p = 10 ** (np.asarray(h) / 10)
    return 10 * math.log10(p[4:].sum() + 1e-12), 10 * math.log10(p[6:].sum() + 1e-12)
cats = {}
for r in refs: cats.setdefault(r["category"], []).append(r["feat"])
def note_for(f0):
    n = int(round(69 + 12 * math.log2(max(f0, 20) / 440.0)))
    while n < 26: n += 12
    while n > 52: n -= 12
    return n
rootOf = {c: note_for(np.median([f["f0"] for f in v])) for c, v in cats.items()}
lines = subprocess.run([BIN, "--dump-presets"], capture_output=True, text=True, check=True).stdout.strip().split("\n")
items = []
for ln in lines:
    name, cat, vals = ln.split("|"); items.append((name, cat, {k: float(x) for k, x in (t.split("=") for t in vals.split())}))
def work(chunk):
    p = subprocess.Popen([BIN, "--serve"], stdin=subprocess.PIPE, stdout=subprocess.PIPE); res = []
    for name, cat, v in chunk:
        root = rootOf.get(cat, 33); row = []
        for pattern in (0, 1):
            p.stdin.write((" ".join("%s=%g" % kv for kv in v.items()) + " _root=%d _bpm=150 _len=%g _pattern=%d\n" % (root, LEN, pattern)).encode()); p.stdin.flush()
            n = int(LEN * SR) * 8; buf = b""
            while len(buf) < n: buf += p.stdout.read(n - len(buf))
            x = np.frombuffer(buf, dtype="<f4").reshape(-1, 2).astype(np.float64)
            f = features(x, SR, f0=440.0 * 2 ** ((root - 69) / 12)); row.append(bright(f["harm"]))
        res.append((name, cat, np.mean([r[0] for r in row]), np.mean([r[1] for r in row])))
    p.kill(); return res
allres = []
with ThreadPoolExecutor(10) as ex:
    for r in ex.map(work, [items[i::10] for i in range(10)]): allres += r
json.dump(allres, open(os.path.join(OUT, "bass_brightness.json"), "w"), ensure_ascii=False)
print("жанр                         | референсы: >6f медиана [10%%]  >24f медиана [10%%] | пресеты: >6f медиана, >24f медиана | пресетов темнее 10%% референсов (по >24f)")
for c in sorted(cats):
    rb = np.array([bright(f["harm"]) for f in cats[c]]); pr = [(n, a, b) for n, cc, a, b in allres if cc == c]
    if not pr: continue
    pa = np.array([[a, b] for _, a, b in pr]); lo24 = np.percentile(rb[:, 1], 10)
    dark = sorted([(b, n) for n, a, b in pr if b < lo24])
    print("%-29s| %6.1f [%6.1f]   %6.1f [%6.1f] | %6.1f, %6.1f | %d из %d" % (c, np.median(rb[:, 0]), np.percentile(rb[:, 0], 10), np.median(rb[:, 1]), lo24, np.median(pa[:, 0]), np.median(pa[:, 1]), len(dark), len(pr)))
    if dark: print("     темнее: " + ", ".join("%s (%.0f)" % (n, b) for b, n in dark[:14]) + (" …" if len(dark) > 14 else ""))
