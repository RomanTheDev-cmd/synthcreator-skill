"""Measures reference bass loops (numbers only; no audio is copied).

Usage: python3 analyze_bass.py OUT.csv DIR_OR_FILE [...]
Needs numpy (PK_NUMPY_PATH may point at a folder that contains it).
"""
import csv, os, sys

if os.environ.get("PK_NUMPY_PATH"):
    sys.path.insert(0, os.environ["PK_NUMPY_PATH"])
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wavread import read_wav  # noqa: E402

STEREO_BANDS = [(20, 100), (100, 250), (250, 600), (600, 1500), (1500, 5000), (5000, 16000)]
SPECTRUM_BANDS = [(20, 60), (60, 120), (120, 300), (300, 1000), (1000, 3000), (3000, 8000), (8000, 20000)]


def band_energy(X, f, lo, hi):
    m = (f >= lo) & (f < hi)
    return float(np.sum(np.abs(X[m]) ** 2))


def db(v):
    return 10 * np.log10(max(v, 1e-30))


def analyze(path):
    x, sr = read_wav(path)
    x = x[: int(sr * 8)]  # first 8 s is plenty for a loop
    if np.max(np.abs(x)) < 1e-4:
        return None
    mid, side = x.mean(axis=1), (x[:, 0] - x[:, 1]) / 2
    n = len(mid)
    win = np.hanning(n)
    M, S = np.fft.rfft(mid * win), np.fft.rfft(side * win)
    f = np.fft.rfftfreq(n, 1.0 / sr)
    total = band_energy(M, f, 20, 20000)
    rms = float(np.sqrt(np.mean(x ** 2)))
    peak = float(np.max(np.abs(x)))
    res = {
        "file": os.path.relpath(path, os.environ.get("PK_ROOT", "/")),
        "rms_db": round(20 * np.log10(rms), 1),
        "crest_db": round(20 * np.log10(peak / rms), 1),
        "dc": round(float(np.mean(mid)), 5),
        "identical_lr": bool(np.max(np.abs(x[:, 0] - x[:, 1])) < 1e-6),
    }
    lowmask = (f >= 25) & (f < 160)
    res["f_peak"] = round(float(f[lowmask][np.argmax(np.abs(M[lowmask]))]), 1)
    for lo, hi in SPECTRUM_BANDS:
        res["e_%d" % lo] = round(db(band_energy(M, f, lo, hi) / total), 1)
    onset = ""
    for lo, hi in STEREO_BANDS:
        ratio = db(band_energy(S, f, lo, hi)) - db(band_energy(M, f, lo, hi))
        res["side_%d" % lo] = round(ratio, 1)
        if onset == "" and ratio > -20:
            onset = lo
    res["stereo_from_hz"] = onset  # lowest band whose side is within 20 dB of mid
    return res


def main():
    out, inputs = sys.argv[1], sys.argv[2:]
    files = []
    for p in inputs:
        if os.path.isdir(p):
            files += sorted(os.path.join(p, f) for f in os.listdir(p) if f.lower().endswith(".wav"))
        else:
            files.append(p)
    rows = []
    for fpath in files:
        try:
            r = analyze(fpath)
            if r:
                rows.append(r)
        except Exception as e:  # noqa: BLE001
            print("skip", fpath, e, file=sys.stderr)
    with open(out, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print(len(rows), "files")


if __name__ == "__main__":
    main()
