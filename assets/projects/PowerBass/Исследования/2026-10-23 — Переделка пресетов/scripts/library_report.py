"""Б1: отчёт по библиотеке PowerBass на текущем движке — яркость против жанра, пауза на долю кика,
чёткость начала нот на роллинговой партии (ноты 1/16 вплотную, пропуск доли кика, 133 BPM), RELEASE.
Запуск: library_report.py TOOLS_DIR POWERBASS_TESTS OUT_DIR"""
import csv, json, math, os, pickle, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor
TOOLS, BIN, OUT = sys.argv[1:4]
sys.path.insert(0, os.path.expanduser("~/Library/Caches/PowerKickTools/py")); sys.path.insert(0, TOOLS)
import numpy as np
from bass_features import features
SR, BPM = 48000, 133.0
refs = pickle.load(open(os.path.join(TOOLS, "..", "Референсы", "bass_features_v2.pkl"), "rb"))
def bright(h):
    p = 10 ** (np.asarray(h) / 10); return 10 * math.log10(p[4:].sum() + 1e-12), 10 * math.log10(p[6:].sum() + 1e-12)
cats = {}
for r in refs: cats.setdefault(r["category"], []).append(r["feat"])
def note_for(f0):
    n = int(round(69 + 12 * math.log2(max(f0, 20) / 440.0)))
    while n < 26: n += 12
    while n > 52: n -= 12
    return n
rootOf = {c: note_for(np.median([f["f0"] for f in v])) for c, v in cats.items()}
refB = {c: np.array([bright(f["harm"]) for f in v]) for c, v in cats.items()}
lines = subprocess.run([BIN, "--dump-presets"], capture_output=True, text=True, check=True).stdout.strip().split("\n")
items = [(l.split("|")[0], l.split("|")[1], {k: float(x) for k, x in (t.split("=") for t in l.split("|")[2].split())}) for l in lines]
step = 60.0 / BPM / 4.0; lead = int(0.3 * SR)
def roll_notes(root):  # 4 такта: пропуск доли кика, затем три 1/16 вплотную (как партия Bass 2)
    out = []
    for k in range(64):
        if k % 4 == 0: continue
        on = lead + int(round(k * step * SR)); off = lead + int(round((k + 1) * step * SR)); out.append((on, off, root, 0.63))
    return out
def band(x, lo, hi):
    F = np.fft.rfft(x); f = np.fft.rfftfreq(len(x), 1 / SR); F[(f < lo) | (f > hi)] = 0; return np.fft.irfft(F, len(x))
def envdb(x, w_ms):
    w = int(SR * w_ms / 1000); return 20 * np.log10(np.sqrt(np.convolve(x * x, np.ones(w) / w, "same")) + 1e-9)
def work(chunk):
    srv = subprocess.Popen([BIN, "--serve"], stdin=subprocess.PIPE, stdout=subprocess.PIPE); res = []
    tmp = tempfile.mkdtemp()
    for name, cat, v in chunk:
        root = rootOf.get(cat, 33); bb = []
        for pattern in (0, 1):
            srv.stdin.write((" ".join("%s=%g" % kv for kv in v.items()) + " _root=%d _bpm=150 _len=3 _pattern=%d\n" % (root, pattern)).encode()); srv.stdin.flush()
            n = int(3 * SR) * 8; buf = b""
            while len(buf) < n: buf += srv.stdout.read(n - len(buf))
            x = np.frombuffer(buf, dtype="<f4").reshape(-1, 2).astype(np.float64)
            bb.append(bright(features(x, SR, f0=440.0 * 2 ** ((root - 69) / 12))["harm"]))
        # роллинговая партия через режим --notes (ноты вплотную)
        pf = os.path.join(tmp, "p.txt"); nf = os.path.join(tmp, "n.txt")
        open(pf, "w").write(" ".join("%s=%g" % kv for kv in v.items())); seq = roll_notes(root)
        open(nf, "w").write("\n".join("%d %d %d %.3f" % s for s in seq))
        raw = subprocess.run([BIN, "--notes", pf, nf, "offfirst", "512"], capture_output=True, check=True).stdout
        x = np.frombuffer(raw, dtype="<f4").reshape(-1, 2).mean(1).astype(np.float64)
        e = envdb(band(x, 40, 4000), 3); s16 = int(step * SR); ons = [s[0] for s in seq]
        gaps, jumps = [], []
        for on in ons:
            seg = e[on: on + s16]
            if not any(abs(on - s16 - o) < 30 for o in ons): gaps.append(e[on - s16 // 2] - seg.max())
            jumps.append(seg.max() - e[on - int(0.002 * SR)])
        res.append({"name": name, "category": cat, "bright6": float(np.mean([b[0] for b in bb])), "bright24": float(np.mean([b[1] for b in bb])),
                    "gap_db": float(np.median(gaps)), "jump_db": float(np.median(jumps)), "release": v.get("release", 0), "sustain": v.get("sustain", 0),
                    "tone": v.get("distTone", 0), "dark": v.get("dark", 0), "cutoff": v.get("cutoff", 0)})
    srv.kill(); return res
rows = []
with ThreadPoolExecutor(10) as ex:
    for r in ex.map(work, [items[i::10] for i in range(10)]): rows += r
order = {n: i for i, (n, _, _) in enumerate(items)}; rows.sort(key=lambda r: order[r["name"]])
for r in rows:
    rb = refB.get(r["category"]); why = []
    if r["category"] in ("Raw Analog Bass", "Hard Techno Rumble Bass"): why.append("референсы жанра — саб/рамбл")
    if rb is not None and r["bright24"] < np.percentile(rb[:, 1], 10): why.append("темнее 10%% жанра")
    if r["release"] >= 390 and r["gap_db"] > -20: why.append("RELEASE 400: бас в паузе на долю кика")
    if r["name"] in ("Reese Core", "Dark Pulse"): why.append("ручной дизайн")
    r["redo"] = "; ".join(why)
with open(os.path.join(OUT, "library_report.csv"), "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
redo = [r for r in rows if r["redo"]]
from collections import Counter
print("пресетов: %d; на переделку: %d" % (len(rows), len(redo)))
print("по жанрам:", dict(Counter(r["category"] for r in redo)))
print("по причинам:", dict(Counter(w for r in redo for w in r["redo"].split("; "))))
print("пауза на долю кика (медиана по библиотеке): %.1f дБ; у RELEASE ≥ 390: %.1f дБ; у остальных: %.1f дБ" % (
    np.median([r["gap_db"] for r in rows]), np.median([r["gap_db"] for r in rows if r["release"] >= 390]), np.median([r["gap_db"] for r in rows if r["release"] < 390] or [0])))
