"""Демо «было — стало»: для каждого изменённого пресета первые два такта партии Bass 2 из проекта Романа
(133 BPM): сначала старая версия, пауза 0,4 с, затем новая. m4a по жанрам и сборный файл на жанр.
  demo_ab.py TOOLS BIN OLD_INC NEW_INC NOTES_JSON OUT_DIR"""
import json, os, subprocess, sys, tempfile, wave
TOOLS, BIN, OLD, NEW, NOTES, OUT = sys.argv[1:7]
sys.path.insert(0, os.path.expanduser("~/Library/Caches/PowerKickTools/py")); sys.path.insert(0, TOOLS)
import numpy as np
from concurrent.futures import ThreadPoolExecutor
import refit_bass as rb
SR, BPM = 48000, 133.0
old = {e["name"]: e for e in rb.library(OLD)}; new = {e["name"]: e for e in rb.library(NEW)}
changed = [n for n in new if n in old and new[n]["params"] != old[n]["params"]]
reg = [o for o in json.load(open(NOTES)) if o["chunk"] == 260714][0]["notes"]
tick = 60.0 / BPM / 960.0 * SR; lead = int(0.05 * SR); bars2 = 2 * 4 * 960
seq = [(lead + int(round((p - 38400) * tick)), lead + int(round((p - 38400 + l) * tick)), pi, v / 127.0) for p, pi, v, l in reg if p - 38400 < bars2]
LEN = lead + int(round(bars2 * tick)) + int(0.25 * SR)
tmp = tempfile.mkdtemp(); nf = os.path.join(tmp, "notes.txt"); open(nf, "w").write("\n".join("%d %d %d %.4f" % s for s in seq))
def render(params, tag):
    pf = os.path.join(tmp, tag + ".txt"); open(pf, "w").write(" ".join("%s=%g" % kv for kv in params.items()))
    raw = subprocess.run([BIN, "--notes", pf, nf, "offfirst", "512"], capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype="<f4").reshape(-1, 2)[:LEN]
def write(path, x):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    w = os.path.join(tmp, os.path.basename(path) + ".wav")
    y = (np.clip(x, -1, 1) * 32767).astype("<i2")
    with wave.open(w, "wb") as f: f.setnchannels(2); f.setsampwidth(2); f.setframerate(SR); f.writeframes(y.tobytes())
    subprocess.run(["afconvert", "-f", "m4af", "-d", "aac", "-b", "192000", w, path], check=True); os.remove(w)
gap = np.zeros((int(0.4 * SR), 2), np.float32)
def one(n):
    a = render(old[n]["params"], "o_" + str(abs(hash(n)))); b = render(new[n]["params"], "n_" + str(abs(hash(n))))
    ab = np.concatenate([a, gap, b]); cat = new[n]["cat"]
    write(os.path.join(OUT, cat, n + ".m4a"), ab)
    return cat, n, ab
with ThreadPoolExecutor(8) as ex:
    res = list(ex.map(one, changed))
bycat = {}
for cat, n, ab in res: bycat.setdefault(cat, []).append((n, ab))
sil = np.zeros((int(0.8 * SR), 2), np.float32)
for cat, lst in bycat.items():
    lst.sort(key=lambda t: t[0])
    write(os.path.join(OUT, "_" + cat + " — обзор.m4a"), np.concatenate(sum([[ab, sil] for _, ab in lst], [])))
    open(os.path.join(OUT, "_" + cat + " — порядок.txt"), "w").write("\n".join("%d. %s" % (i + 1, n) for i, (n, _) in enumerate(lst)) + "\n")
print("изменённых пресетов: %d; жанров: %d" % (len(changed), len(bycat)))
