"""Тональность середины (300–3000 Гц) в окнах 15–60 и 60–140 мс: плоскость спектра (0 — чистые тоны, 1 — шум). Референсы против пресетов, по жанрам."""
import os, pickle, subprocess, sys, random, json
TOOLS, BIN, RROOT, OUT = sys.argv[1:5]
sys.path.insert(0, os.path.expanduser("~/Library/Caches/PowerKickTools/py")); sys.path.insert(0, TOOLS)
import numpy as np
from wavread import read_wav
SR = 48000
def flat(m, sr, t0, t1):
    seg = m[int(t0 * sr): int(t1 * sr)]; n = 1 << 15
    X = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n)) ** 2 + 1e-30; fr = np.fft.rfftfreq(n, 1 / sr)
    b = X[(fr >= 300) & (fr <= 3000)]
    return float(np.exp(np.mean(np.log(b))) / np.mean(b))
def meas(x, sr):
    m = x.mean(1) if x.ndim == 2 else x; a = np.abs(m); m = m[int(np.argmax(a > a.max() * 0.1)):]
    return flat(m, sr, 0.015, 0.06), flat(m, sr, 0.06, 0.14)
refs = pickle.load(open(os.path.join(TOOLS, "..", "Референсы", "kick_features.pkl"), "rb"))
random.seed(2); random.shuffle(refs)
RV = {}
for r in refs:
    c = r["category"]
    if len(RV.get(c, [])) >= 40: continue
    path = os.path.join(RROOT, r["file"])
    if not os.path.exists(path): continue
    try: x, sr = read_wav(path)
    except Exception: continue
    RV.setdefault(c, []).append(meas(np.asarray(x, float), sr))
lines = subprocess.run([BIN, "--dump-presets"], capture_output=True, text=True, check=True).stdout.strip().split("\n")
items = [(ln.split("|")[0], ln.split("|")[1], {k: float(x) for k, x in (t.split("=") for t in ln.split("|")[2].split())}) for ln in lines]
p = subprocess.Popen([BIN, "--serve"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
PV = {}; per = []
for name, cat, v in items:
    p.stdin.write((" ".join("%s=%g" % kv for kv in v.items()) + " _len=0.8 _sr=48000\n").encode()); p.stdin.flush()
    n = int(0.8 * SR) * 8; buf = b""
    while len(buf) < n: buf += p.stdout.read(n - len(buf))
    mm = meas(np.frombuffer(buf, dtype="<f4").reshape(-1, 2).astype(np.float64), SR)
    PV.setdefault(cat, []).append(mm); per.append((name, cat, mm, v.get("clickType"), v.get("drive"), v.get("driveMode")))
p.kill()
json.dump({"presets": per, "ref_p10_60_140": {c: float(np.percentile(np.array(v)[:, 1], 10)) for c, v in RV.items()}}, open(os.path.join(OUT, "kick_tonality.json"), "w"), ensure_ascii=False)
print("Плоскость спектра 300–3000 Гц (медиана [25–75%]); ниже = тональнее, «синтовее/звонче»:")
for c in sorted(RV):
    r = np.array(RV[c]); pv = np.array(PV.get(c, [(np.nan, np.nan)]))
    lo = np.percentile(r[:, 1], 10)
    print("  %-12s 15–60 мс: реф. %.3f [%.3f–%.3f], пресеты %.3f [%.3f–%.3f] | 60–140 мс: реф. %.3f [%.3f–%.3f], пресеты %.3f [%.3f–%.3f] | пресетов тональнее 10%% реф.: %d из %d" % (
        c, np.median(r[:, 0]), *np.percentile(r[:, 0], (25, 75)), np.median(pv[:, 0]), *np.percentile(pv[:, 0], (25, 75)),
        np.median(r[:, 1]), *np.percentile(r[:, 1], (25, 75)), np.median(pv[:, 1]), *np.percentile(pv[:, 1], (25, 75)), int((pv[:, 1] < lo).sum()), len(pv)))
