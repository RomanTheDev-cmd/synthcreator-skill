"""Б2: кандидаты в референсы «Raw Analog Bass» из синт-папок TSOHT, не вошедших в набор.
Оставляет басовый регистр (доля энергии ниже 200 Гц не меньше −8 дБ, основной тон 28..130 Гц) и считает яркость."""
import os, sys, math, pickle
TOOLS = sys.argv[1]; OUT = sys.argv[2]
sys.path.insert(0, os.path.expanduser("~/Library/Caches/PowerKickTools/py")); sys.path.insert(0, TOOLS)
import numpy as np
from wavread import read_wav
from bass_features import features
from concurrent.futures import ThreadPoolExecutor
ROOT = os.path.expanduser("~/Library/Mobile Documents/com~apple~CloudDocs/definitionofhardtechno")
KEYS = ["ITHTS Synth Sequences", "SSP Rhythm Synth Lines", "SSP Driving Synth Sequences", "OBI Dark Industrial Synthlines",
        "HST1 Synth Hook Loops", "Rhythm Synth Hooks", "Raving Synth Lines", "SSP Processed Synth Loops", "ARK Weird Synth Loops",
        "Synth Loops"]
files = []
for dirpath, _, names in os.walk(ROOT):
    rel = os.path.relpath(dirpath, ROOT)
    if "DEMO" in rel or "MIDI" in rel or "Bass" in rel: continue
    if any(k in rel for k in KEYS):
        files += [os.path.join(rel, n) for n in sorted(names) if n.lower().endswith(".wav")]
def low_share(x, sr):
    m = x.mean(1); n = len(m); M = np.abs(np.fft.rfft(m * np.hanning(n))) ** 2; f = np.fft.rfftfreq(n, 1 / sr)
    return 10 * math.log10(M[(f >= 20) & (f < 200)].sum() / (M[(f >= 20) & (f < 20000)].sum() + 1e-20) + 1e-20)
def one(rel):
    try:
        x, sr = read_wav(os.path.join(ROOT, rel)); x = np.asarray(x, float)
        if x.ndim == 1: x = np.stack([x, x], 1)
        f = features(x, sr)
        if f is None: return None
        p = 10 ** (np.asarray(f["harm"]) / 10)
        return {"file": rel, "low": low_share(x[: int(sr * 8)], sr), "f0": f["f0"], "b6": 10 * math.log10(p[4:].sum() + 1e-12),
                "b24": 10 * math.log10(p[6:].sum() + 1e-12), "motion": f["motion"], "sustain": f["sustain"], "feat": f}
    except Exception as e:
        return None
with ThreadPoolExecutor(8) as ex:
    res = [r for r in ex.map(one, files) if r]
bass = [r for r in res if r["low"] >= -8 and r["f0"] <= 130]
pickle.dump(bass, open(os.path.join(OUT, "raw_analog_candidates.pkl"), "wb"))
from collections import Counter
print("файлов просмотрено: %d, с признаками: %d, басовый регистр: %d" % (len(files), len(res), len(bass)))
print("по папкам:", dict(Counter(r["file"].split("/")[-2][-40:] for r in bass)))
b = np.array([[r["b6"], r["b24"]] for r in bass]) if bass else np.zeros((0, 2))
if len(b): print("яркость кандидатов: >6f медиана %.1f [10%% %.1f], >24f медиана %.1f [10%% %.1f]" % (np.median(b[:, 0]), np.percentile(b[:, 0], 10), np.median(b[:, 1]), np.percentile(b[:, 1], 10)))
for r in sorted(bass, key=lambda r: -r["b6"])[:12]: print("   f0 %5.1f низ %5.1f >6f %6.1f >24f %6.1f | %s" % (r["f0"], r["low"], r["b6"], r["b24"], r["file"][-80:]))
