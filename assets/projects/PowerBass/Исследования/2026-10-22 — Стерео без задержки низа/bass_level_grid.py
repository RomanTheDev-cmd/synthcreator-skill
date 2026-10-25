"""Пик каждого пресета PowerBass по сетке нормировки (Tools/normalize_peaks.py): 2 риффа на 5 корнях и
выдержанная нота на каждом полутоне E1–C3, 150 BPM, 3 с. Пик считается при текущем MASTER (замер на −40 дБ и
линейный пересчёт, как в нормировке), для старой и новой сборки."""
import json, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.expanduser("~/Library/Caches/PowerKickTools/py"))
import numpy as np

OLD, NEW, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
SR, LEN = 48000, 3.0
JOBS = [(root, pat) for root in (28, 33, 38, 43, 48) for pat in (0, 1)] + [(root, 2) for root in range(28, 49)]


def peak(path, items):
    p = subprocess.Popen([path, "--serve"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    res = []
    for name, v in items:
        probe = {**v, "master": -40.0}
        worst = -1e9
        for root, pat in JOBS:
            line = " ".join("%s=%g" % kv for kv in probe.items()) + " _root=%d _bpm=150 _len=%g _pattern=%d\n" % (root, LEN, pat)
            p.stdin.write(line.encode()); p.stdin.flush()
            n = int(LEN * SR) * 2 * 4; buf = b""
            while len(buf) < n:
                c = p.stdout.read(n - len(buf))
                if not c: raise RuntimeError("renderer ended")
                buf += c
            x = np.frombuffer(buf, dtype="<f4")
            worst = max(worst, 20 * np.log10(np.abs(x).max()))
        res.append((name, float(worst - (-40.0) + v["master"])))
    p.kill()
    return res


out = subprocess.run([OLD, "--dump-presets"], capture_output=True, text=True, check=True).stdout.strip().split("\n")
items = []
for line in out:
    name, cat, vals = line.split("|")
    items.append((name, {k: float(x) for k, x in (t.split("=") for t in vals.split())}))
chunks = [items[i::6] for i in range(6)]
with ThreadPoolExecutor(12) as ex:
    fo = [ex.submit(peak, OLD, c) for c in chunks]
    fn = [ex.submit(peak, NEW, c) for c in chunks]
    old = dict(sum((f.result() for f in fo), []))
    new = dict(sum((f.result() for f in fn), []))
rows = [{"preset": n, "old_peak_dbfs": old[n], "new_peak_dbfs": new[n]} for n, _ in items]
json.dump(rows, open(os.path.join(OUT, "bass_level_grid.json"), "w"), ensure_ascii=False, indent=1)
o = np.array([r["old_peak_dbfs"] for r in rows]); nw = np.array([r["new_peak_dbfs"] for r in rows]); d = nw - o
print("старая сборка: пик от %.2f до %.2f дБFS" % (o.min(), o.max()))
print("новая сборка:  пик от %.2f до %.2f дБFS" % (nw.min(), nw.max()))
print("изменение пика: медиана %+.2f, мин %+.2f, макс %+.2f дБ" % (np.median(d), d.min(), d.max()))
print("новая: выше порога защиты −0.9 дБFS: %d; в (−1.1, −0.9]: %d; ниже −1.1: %d (ниже −2: %d)" % (
    (nw > -0.9).sum(), ((nw > -1.1) & (nw <= -0.9)).sum(), (nw <= -1.1).sum(), (nw < -2).sum()))
for r in sorted(rows, key=lambda r: -r["new_peak_dbfs"])[:5]:
    print("  самые громкие: %-22s %.2f → %.2f" % (r["preset"], r["old_peak_dbfs"], r["new_peak_dbfs"]))
