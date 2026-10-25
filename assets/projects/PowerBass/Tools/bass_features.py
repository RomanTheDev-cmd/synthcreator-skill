"""Pitch-independent bass-loop features and a distance between two basses.

  harm    - energy in bands relative to the fundamental f (0.35f..0.7f sub, then octaves up to 96f),
            dB relative to the total (shape of the harmonic spectrum: clean sub .. dirty bright)
  crest   - peak / RMS of the whole excerpt (dB): density
  sustain - fraction of 10 ms frames within 12 dB of the loudest frame: legato vs plucky
  motion  - std of the spectral centroid over 20 ms frames (octaves): filter/FM movement
  side    - side/mid in 6 absolute bands (dB, clipped -40..0)
All levels are relative (to total or peak), never absolute loudness.
"""
import numpy as np

SIDE_BANDS = [(20, 100), (100, 250), (250, 600), (600, 1500), (1500, 5000), (5000, 16000)]
HARM_MULT = [0.35, 0.7, 1.5, 3, 6, 12, 24, 48, 96]


def fundamental(mid, sr):
    n = len(mid)
    M = np.abs(np.fft.rfft(mid * np.hanning(n)))
    f = np.fft.rfftfreq(n, 1 / sr)
    m = (f >= 28) & (f < 170)
    return float(f[m][np.argmax(M[m])])


def features(x, sr, f0=None):
    x = x[: int(sr * 8)]
    mid, side = x.mean(axis=1), (x[:, 0] - x[:, 1]) / 2
    if np.max(np.abs(mid)) < 1e-5:
        return None
    n = len(mid)
    win = np.hanning(n)
    M, S = np.fft.rfft(mid * win), np.fft.rfft(side * win)
    f = np.fft.rfftfreq(n, 1 / sr)
    P = np.abs(M) ** 2
    total = np.sum(P[(f >= 20) & (f < 20000)]) + 1e-30
    f0 = f0 or fundamental(mid, sr)
    harm = []
    for lo, hi in zip(HARM_MULT[:-1], HARM_MULT[1:]):
        mm = (f >= lo * f0) & (f < min(hi * f0, 20000))
        harm.append(10 * np.log10(np.sum(P[mm]) / total + 1e-12))
    mm = f >= HARM_MULT[-1] * f0
    harm.append(10 * np.log10(np.sum(P[mm & (f < 20000)]) / total + 1e-12))
    harm = np.maximum(np.array(harm), -80)

    hop = int(0.01 * sr)
    fr = np.array([np.sqrt(np.mean(mid[i:i + hop] ** 2)) for i in range(0, n - hop, hop)])
    frdb = 20 * np.log10(fr + 1e-9)
    sustain = float(np.mean(frdb > np.max(frdb) - 12))
    crest = float(20 * np.log10(np.max(np.abs(mid)) / (np.sqrt(np.mean(mid ** 2)) + 1e-12)))

    hop2 = int(0.02 * sr)
    cents = []
    for i in range(0, n - hop2, hop2):
        seg = mid[i:i + hop2]
        if np.sqrt(np.mean(seg ** 2)) < np.max(fr) * 0.1:
            continue
        Q = np.abs(np.fft.rfft(seg * np.hanning(hop2))) ** 2
        ff = np.fft.rfftfreq(hop2, 1 / sr)
        cents.append(np.log2(np.sum(Q * ff) / (np.sum(Q) + 1e-20) + 1e-9))
    motion = float(np.std(cents)) if len(cents) > 3 else 0.0

    st = []
    for lo, hi in SIDE_BANDS:
        mm = (f >= lo) & (f < hi)
        st.append(np.clip(10 * np.log10((np.sum(np.abs(S[mm]) ** 2) + 1e-30) / (np.sum(P[mm]) + 1e-30)), -40, 0))
    return {"f0": f0, "harm": harm, "crest": crest, "sustain": sustain, "motion": motion, "side": np.array(st)}


def distance(a, b, detail=False):
    parts = {
        "harm": float(np.mean(np.abs(a["harm"] - b["harm"]))),
        "crest": abs(a["crest"] - b["crest"]),
        "sustain": 10.0 * abs(a["sustain"] - b["sustain"]),
        "motion": 6.0 * abs(a["motion"] - b["motion"]),
        # Below 100 Hz PowerBass is mono by design (club mono-compatibility), so many wide
        # reference loops are deliberately not matched there.
        "side": float(np.mean(np.abs(a["side"][1:] - b["side"][1:]))),
    }
    w = {"harm": 1.0, "crest": 0.7, "sustain": 0.5, "motion": 0.5, "side": 0.4}
    total = sum(w[k] * v for k, v in parts.items())
    return (total, parts) if detail else total
