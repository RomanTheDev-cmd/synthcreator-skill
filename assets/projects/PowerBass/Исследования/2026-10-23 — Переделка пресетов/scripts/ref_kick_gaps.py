"""Есть ли у референсных лупов пауза на долю кика: уровень (40..4000 Гц, окно 10 мс) в первые 40 мс каждой доли
относительно медианы уровня остальной части доли. Темп — из имени файла (NNNBPM / NNN bpm), начало лупа — начало такта."""
import os, re, sys, pickle, math
TOOLS = sys.argv[1]
sys.path.insert(0, os.path.expanduser("~/Library/Caches/PowerKickTools/py")); sys.path.insert(0, TOOLS)
import numpy as np
from wavread import read_wav
from concurrent.futures import ThreadPoolExecutor
ROOT = os.path.expanduser("~/Library/Mobile Documents/com~apple~CloudDocs/definitionofhardtechno")
refs = pickle.load(open(os.path.join(TOOLS, "..", "Референсы", "bass_features_v2.pkl"), "rb"))
def bpm_of(name):
    m = re.search(r"(\d{3})\s*BPM", name, re.I) or re.search(r" (1[2-7]\d) ", name)
    return float(m.group(1)) if m else None
def one(r):
    bpm = bpm_of(r["file"].split("/")[-1])
    if not bpm: return None
    try:
        x, sr = read_wav(os.path.join(ROOT, r["file"])); x = np.asarray(x, float); m = x.mean(1) if x.ndim == 2 else x
    except Exception: return None
    F = np.fft.rfft(m); f = np.fft.rfftfreq(len(m), 1 / sr); F[(f < 40) | (f > 4000)] = 0; y = np.fft.irfft(F, len(m))
    w = int(0.01 * sr); e = 20 * np.log10(np.sqrt(np.convolve(y * y, np.ones(w) / w, "same")) + 1e-9)
    beat = 60.0 / bpm; out = []
    for k in range(int(len(m) / sr / beat) - 1):
        a = int(k * beat * sr); g = e[a: a + int(0.04 * sr)].mean(); rest = np.median(e[a + int(0.12 * sr): a + int(beat * sr)])
        out.append(g - rest)
    return (r["category"], float(np.median(out))) if out else None
with ThreadPoolExecutor(8) as ex:
    res = [x for x in ex.map(one, refs) if x]
cats = {}
for c, g in res: cats.setdefault(c, []).append(g)
print("пауза на долю кика в референсах (дБ относительно остальной доли; меньше = глубже пауза):")
for c in sorted(cats):
    v = np.array(cats[c]); print("  %-26s n=%3d  медиана %+6.1f  [25%% %+6.1f, 75%% %+6.1f]  глубже −10 дБ: %d" % (c, len(v), np.median(v), np.percentile(v, 25), np.percentile(v, 75), int((v < -10).sum())))
pickle.dump(res, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ref_kick_gaps.pkl"), "wb"))
