"""Неармоничность звона 60–140 мс: Schranz-референсы против Schranz-пресетов; пресеты с Metal Click и с ним же заменённым на Sharp."""
import os, pickle, subprocess, sys, random
TOOLS, BIN, RROOT = sys.argv[1], sys.argv[2], sys.argv[3]
sys.path.insert(0, os.path.expanduser("~/Library/Caches/PowerKickTools/py")); sys.path.insert(0, TOOLS)
import numpy as np
from wavread import read_wav
from kick_features import features, F0_TIMES
SR = 48000
def onset(m):
    a = np.abs(m); return int(np.argmax(a > a.max() * 0.1))
def inharm(x, sr):
    m = x.mean(1) if x.ndim == 2 else x; m = m[onset(m):]
    f = features(np.stack([m, m], 1), sr); tr = f["f0"]; sel = (F0_TIMES >= 0.06) & (F0_TIMES <= 0.14)
    if np.isfinite(tr[sel]).sum() < 5: return np.nan
    f0 = 55 * 2 ** (np.nanmedian(tr[sel]) / 12)
    seg = m[int(0.06 * sr): int(0.14 * sr)]; n = 1 << 16
    X = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n)); fr = np.fft.rfftfreq(n, 1 / sr)
    band = (fr >= 300) & (fr <= 3000); idx = np.where(band)[0]; P = X[idx] ** 2
    pk = [i for i in range(1, len(P) - 1) if P[i] > P[i - 1] and P[i] > P[i + 1]]
    pk = sorted(pk, key=lambda i: -P[i])[:12]
    if not pk: return np.nan
    w = np.array([P[i] for i in pk]); dev = np.array([abs(fr[idx[i]] / f0 - round(fr[idx[i]] / f0)) for i in pk])
    return float((w * dev).sum() / w.sum())
refs = [r for r in pickle.load(open(os.path.join(TOOLS, "..", "Референсы", "kick_features.pkl"), "rb")) if r["category"] == "Schranz"]
random.seed(1); random.shuffle(refs)
rv = []
for r in refs:
    path = os.path.join(RROOT, r["file"])
    if not os.path.exists(path): continue
    try:
        x, sr = read_wav(path)
    except Exception as e:
        continue
    v = inharm(np.asarray(x, float), sr)
    if np.isfinite(v): rv.append(v)
    if len(rv) >= 40: break
lines = subprocess.run([BIN, "--dump-presets"], capture_output=True, text=True, check=True).stdout.strip().split("\n")
items = [(ln.split("|")[0], ln.split("|")[1], {k: float(x) for k, x in (t.split("=") for t in ln.split("|")[2].split())}) for ln in lines]
p = subprocess.Popen([BIN, "--serve"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
def render(v):
    p.stdin.write((" ".join("%s=%g" % kv for kv in v.items()) + " _len=0.8 _sr=48000\n").encode()); p.stdin.flush()
    n = int(0.8 * SR) * 8; buf = b""
    while len(buf) < n: buf += p.stdout.read(n - len(buf))
    return np.frombuffer(buf, dtype="<f4").reshape(-1, 2).astype(np.float64)
metal, metal_sharp, other = [], [], []
for name, cat, v in items:
    if cat != "Schranz": continue
    a = inharm(render(v), SR)
    if v.get("clickType") == 2:
        metal.append(a); metal_sharp.append(inharm(render({**v, "clickType": 1}), SR))
    else: other.append(a)
p.kill()
q = lambda a: "медиана %.3f, 25–75%%: %.3f–%.3f (n=%d)" % (np.nanmedian(a), np.nanpercentile(a, 25), np.nanpercentile(a, 75), np.isfinite(a).sum())
print("Неармоничность пиков 300–3000 Гц в 60–140 мс (0 — гармонический ряд тела, ~0,25 — случайные частоты):")
print("  Schranz-референсы:                 ", q(np.array(rv)))
print("  Schranz-пресеты с Metal Click:     ", q(np.array(metal)))
print("  те же, Metal заменён на Sharp:     ", q(np.array(metal_sharp)))
print("  Schranz-пресеты без Metal:         ", q(np.array(other)))
