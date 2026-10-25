"""Time-resolved kick features and a perceptual-ish distance between two kicks.

Features (onset-aligned, sample-rate independent):
  env   - RMS level in 5 ms frames, 0..600 ms, dB relative to the kick's sample peak (floor -70)
  f0    - fundamental track 4..200 ms (later the track is unreliable on distorted tails) in semitones re 55 Hz (NaN where undefined)
  spec  - band levels (12 log bands, 20 Hz..20 kHz) in 4 time segments, dB relative to the peak

Everything is relative to the peak: mastered references are compared by shape and density
(body level against the transient), not by how loud they were mastered.
  side  - side/mid ratio in 3 bands (dB, clipped -40..0)
"""
import numpy as np

SEGMENTS = [(0.0, 0.012), (0.012, 0.045), (0.045, 0.14), (0.14, 0.40)]
BAND_EDGES = np.geomspace(20, 20000, 13)
STEREO_BANDS = [(20, 120), (120, 1000), (1000, 16000)]
ENV_FRAME = 0.005
ENV_LEN = 0.6
F0_TIMES = np.arange(0.004, 0.200, 0.004)
FLOOR = -70.0


def _db(power):
    return np.maximum(10 * np.log10(np.maximum(power, 1e-30)), FLOOR)


def _onset(mono):
    peak = np.max(np.abs(mono))
    i = int(np.argmax(np.abs(mono) > peak * 0.1))
    return i


def _band_levels(x, sr):
    """Mean power per log band of segment x (dBFS)."""
    n = max(len(x), 64)
    X = np.fft.rfft(x * np.hanning(len(x)), n)
    f = np.fft.rfftfreq(n, 1 / sr)
    p = np.abs(X) ** 2 / (np.sum(np.hanning(len(x)) ** 2) + 1e-12) * 2.0
    out = []
    for lo, hi in zip(BAND_EDGES[:-1], BAND_EDGES[1:]):
        m = (f >= lo) & (f < hi)
        out.append(_db(np.sum(p[m]) if np.any(m) else 0.0))
    return np.array(out)


def _lowpass(x, sr, hi):
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1 / sr)
    X[f > hi] = 0
    X[f < 15] = 0
    return np.fft.irfft(X, len(x))


def _f0_track(mono, sr):
    """Instantaneous frequency from rising zero crossings of the <500 Hz part."""
    seg = mono[: int(0.35 * sr)]
    low = _lowpass(seg, sr, 500.0)
    s = np.sign(low)
    idx = np.where((s[:-1] < 0) & (s[1:] >= 0))[0]
    if len(idx) < 3:
        return np.full(len(F0_TIMES), np.nan)
    # sub-sample crossing positions
    t = (idx + low[idx] / (low[idx] - low[idx + 1] + 1e-20)) / sr
    period = np.diff(t)
    mids = (t[:-1] + t[1:]) / 2
    freq = 1.0 / period
    ok = (freq > 20) & (freq < 1500)
    if np.sum(ok) < 2:
        return np.full(len(F0_TIMES), np.nan)
    tr = np.interp(F0_TIMES, mids[ok], freq[ok], left=np.nan, right=np.nan)
    return 12 * np.log2(tr / 55.0)


def _tonal(mono, sr):
    """Spectral flatness of the mids (300..3000 Hz) 15..60 ms and 60..140 ms after the onset, in dB (0 = noise,
    very negative = pure tones). Reference kicks are noisy there; a kick whose mids ring as a harmonic series
    sounds like a synth note or a bell (Roman's listening test, 2026-10-23)."""
    out = []
    for t0, t1 in ((0.015, 0.06), (0.06, 0.14)):
        seg = mono[int(t0 * sr): int(t1 * sr)]
        n = 1 << 15
        X = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n)) ** 2 + 1e-30
        f = np.fft.rfftfreq(n, 1 / sr)
        b = X[(f >= 300) & (f <= 3000)]
        out.append(10 * np.log10(np.exp(np.mean(np.log(b))) / np.mean(b)))
    return np.array(out)


def features(x, sr):
    """x: (n, 2) float array."""
    mono = x.mean(axis=1)
    if np.max(np.abs(mono)) < 1e-5:
        return None
    on = max(0, _onset(mono) - int(0.001 * sr))
    x = x[on:]
    mono = mono[on:]
    need = int(ENV_LEN * sr) + 1
    if len(mono) < need:
        pad = need - len(mono)
        mono = np.pad(mono, (0, pad))
        x = np.pad(x, ((0, pad), (0, 0)))
    hop = int(ENV_FRAME * sr)
    frames = int(ENV_LEN / ENV_FRAME)
    peak_db = 20 * np.log10(np.max(np.abs(mono)))
    env = np.maximum(np.array([_db(np.mean(mono[i * hop:(i + 1) * hop] ** 2)) for i in range(frames)]) - peak_db, FLOOR)
    spec = np.maximum(np.stack([_band_levels(mono[int(a * sr):int(b * sr)], sr) for a, b in SEGMENTS]) - peak_db, FLOOR)
    mid, side = x[: int(0.5 * sr)].mean(axis=1), (x[: int(0.5 * sr), 0] - x[: int(0.5 * sr), 1]) / 2
    M, S = np.fft.rfft(mid), np.fft.rfft(side)
    f = np.fft.rfftfreq(len(mid), 1 / sr)
    st = []
    for lo, hi in STEREO_BANDS:
        m = (f >= lo) & (f < hi)
        st.append(np.clip(10 * np.log10((np.sum(np.abs(S[m]) ** 2) + 1e-30) / (np.sum(np.abs(M[m]) ** 2) + 1e-30)), -40, 0))
    return {"env": env, "f0": _f0_track(mono, sr), "spec": spec, "side": np.array(st), "peak_db": peak_db,
            "tonal": _tonal(mono, sr)}


WEIGHTS = {"env": 1.0, "f0": 1.5, "spec": 1.0, "side": 0.15, "tonal": 1.5}


def _resolvable():
    """Spectral cells whose band is resolvable in the segment window (>= 2.5 periods of the band's low edge)."""
    m = np.zeros((len(SEGMENTS), len(BAND_EDGES) - 1), bool)
    for i, (t0, t1) in enumerate(SEGMENTS):
        m[i] = BAND_EDGES[:-1] * (t1 - t0) >= 2.5
    return m


VALID = _resolvable()


def distance(a, b, detail=False):
    """Mean absolute differences (dB / semitones), weighted. a = candidate, b = target."""
    live = (a["env"] > -60) | (b["env"] > -60)
    d_env = float(np.mean(np.abs(a["env"][live] - b["env"][live]))) if np.any(live) else 0.0
    both = ~np.isnan(a["f0"]) & ~np.isnan(b["f0"])
    only = np.isnan(a["f0"]) ^ np.isnan(b["f0"])
    d_f0 = (float(np.mean(np.abs(a["f0"][both] - b["f0"][both]))) if np.any(both) else 12.0) + 6.0 * float(np.mean(only))
    d_spec = float(np.mean(np.abs(a["spec"] - b["spec"])[VALID]))
    d_side = float(np.mean(np.abs(a["side"] - b["side"])))
    parts = {"env": d_env, "f0": d_f0, "spec": d_spec, "side": d_side}
    if "tonal" in a and "tonal" in b:  # older feature files have no tonality
        parts["tonal"] = float(np.mean(np.abs(a["tonal"] - b["tonal"])))
    total = sum(WEIGHTS[k] * v for k, v in parts.items())
    return (total, parts) if detail else total
