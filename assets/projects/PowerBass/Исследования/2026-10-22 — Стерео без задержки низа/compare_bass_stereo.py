"""PowerBass: влияние новой схемы центра стерео на 300 пресетов (старая сборка против новой).

Для каждого пресета: ролл 16-х на C2/D#2 (_pattern 0, корень 38) и выдержанная C2 (_pattern 2, корень 36).
Метрики: пик и RMS (при MASTER −40 дБ, до защиты), side/mid выше 200 Гц, наибольшее отличие спектра
mid по третьоктавным полосам 40 Гц – 8 кГц (в полосах не тише −40 дБ от самой громкой).
"""
import json, os, subprocess, sys
sys.path.insert(0, os.path.expanduser("~/Library/Caches/PowerKickTools/py"))
import numpy as np

OLD, NEW, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
SR = 48000


def server(path):
    return subprocess.Popen([path, "--serve"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)


def render(s, params, root, pattern, length=3.0):
    line = " ".join("%s=%g" % kv for kv in params.items()) + " _root=%d _bpm=150 _len=%g _pattern=%d\n" % (root, length, pattern)
    s.stdin.write(line.encode()); s.stdin.flush()
    n = int(length * SR) * 2 * 4; buf = b""
    while len(buf) < n:
        c = s.stdout.read(n - len(buf))
        if not c: raise RuntimeError("renderer ended")
        buf += c
    return np.frombuffer(buf, dtype="<f4").reshape(-1, 2).astype(np.float64)


def bands(x):
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1 / SR)
    centres = 40 * 2 ** (np.arange(0, 28) / 3)
    out = []
    for c in centres:
        m = (f >= c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))
        out.append(10 * np.log10(spec[m].sum() + 1e-30))
    return np.array(out)


def band_rms(x, lo, hi):
    spec = np.abs(np.fft.rfft(x)) ** 2
    f = np.fft.rfftfreq(len(x), 1 / SR)
    return 10 * np.log10(spec[(f >= lo) & (f < hi)].sum() + 1e-30)


out = subprocess.run([OLD, "--dump-presets"], capture_output=True, text=True, check=True).stdout.strip().split("\n")
a, b = server(OLD), server(NEW)
rows = []
for line in out:
    name, cat, vals = line.split("|")
    v = {k: float(x) for k, x in (t.split("=") for t in vals.split())}
    probe = {**v, "master": -40.0}
    row = {"preset": name, "category": cat, "width": v.get("width", 0.0), "split": v.get("splitHz", v.get("split", 0.0))}
    for label, root, pat in (("roll", 38, 0), ("held", 36, 2)):
        x, y = render(a, probe, root, pat), render(b, probe, root, pat)
        row[label + "_peak_db"] = 20 * np.log10(np.abs(y).max() / np.abs(x).max())
        row[label + "_rms_db"] = 10 * np.log10((y ** 2).mean() / (x ** 2).mean())
        mx, my = x.mean(1), y.mean(1)
        sx, sy = 0.5 * (x[:, 0] - x[:, 1]), 0.5 * (y[:, 0] - y[:, 1])
        row[label + "_sidemid_old"] = band_rms(sx, 200, 16000) - band_rms(mx, 200, 16000)
        row[label + "_sidemid_new"] = band_rms(sy, 200, 16000) - band_rms(my, 200, 16000)
        bx, by = bands(mx), bands(my)
        keep = bx > bx.max() - 40
        row[label + "_band_maxdiff_db"] = float(np.max(np.abs(by - bx)[keep]))
    rows.append(row)
a.kill(); b.kill()
json.dump(rows, open(os.path.join(OUT, "bass_stereo_compare.json"), "w"), ensure_ascii=False, indent=1)


def stat(key):
    v = np.array([r[key] for r in rows])
    return "медиана %+.2f, 90%% %+.2f, мин %+.2f, макс %+.2f" % (np.median(v), np.percentile(v, 90), v.min(), v.max())


for label in ("roll", "held"):
    print("==", label)
    for key in ("_peak_db", "_rms_db", "_band_maxdiff_db"):
        print("  ", key, stat(label + key))
    d = np.array([r[label + "_sidemid_new"] - r[label + "_sidemid_old"] for r in rows])
    print("   side/mid изменение, дБ: медиана %+.2f, мин %+.2f, макс %+.2f" % (np.median(d), d.min(), d.max()))
widths = np.array([r["width"] for r in rows])
print("WIDTH пресетов: медиана %.2f, доля с WIDTH 0: %.0f%%" % (np.median(widths), 100 * np.mean(widths == 0)))
