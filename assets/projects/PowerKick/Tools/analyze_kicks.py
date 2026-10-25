"""Measures reference kick samples (numbers only; no audio is copied).

Usage: python3 analyze_kicks.py OUT.csv FILE_OR_DIR [...]
Needs numpy (pass its location in PK_NUMPY_PATH if not installed).
"""
import csv, os, struct, sys

if os.environ.get("PK_NUMPY_PATH"):
    sys.path.insert(0, os.environ["PK_NUMPY_PATH"])
import numpy as np


def read_wav(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not wav")
    pos, fmt, raw = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            tag, ch, sr = struct.unpack("<HHI", body[:8])
            bits = struct.unpack("<H", body[14:16])[0]
            if tag == 0xFFFE:
                tag = struct.unpack("<H", body[24:26])[0]
            fmt = (tag, ch, sr, bits)
        elif cid == b"data":
            raw = body
        pos += 8 + size + (size & 1)
    tag, ch, sr, bits = fmt
    if tag == 3:
        x = np.frombuffer(raw[: len(raw) // (bits // 8) * (bits // 8)], dtype="<f4" if bits == 32 else "<f8").astype(np.float64)
    elif bits == 16:
        x = np.frombuffer(raw[: len(raw) // 2 * 2], dtype="<i2") / 32768.0
    elif bits == 24:
        b = np.frombuffer(raw[: len(raw) // 3 * 3], dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        v = np.where(v >= 1 << 23, v - (1 << 24), v)
        x = v / 8388608.0
    elif bits == 32:
        x = np.frombuffer(raw[: len(raw) // 4 * 4], dtype="<i4") / 2147483648.0
    else:
        raise ValueError("bits %d" % bits)
    x = x[: len(x) // ch * ch].reshape(-1, ch)
    if ch == 1:
        x = np.repeat(x, 2, axis=1)
    return x[:, :2], sr


def band(x, sr, lo, hi):
    """Zero-phase FFT brick-wall band filter (analysis only)."""
    n = len(x)
    X = np.fft.rfft(x, axis=0)
    f = np.fft.rfftfreq(n, 1.0 / sr)
    X[(f < lo) | (f >= hi)] = 0
    return np.fft.irfft(X, n, axis=0)


def db(v):
    return 20 * np.log10(max(v, 1e-12))


def rms(v):
    return float(np.sqrt(np.mean(v * v))) if len(v) else 0.0


def zc_freq(y, sr):
    z = np.where((y[:-1] < 0) & (y[1:] >= 0))[0]
    return (len(z) - 1) * sr / (z[-1] - z[0]) if len(z) > 2 else 0.0


def analyze(path):
    x, sr = read_wav(path)
    mono = x.mean(axis=1)
    peak = float(np.max(np.abs(x)))
    if peak < 1e-4:
        return None
    on = int(np.argmax(np.abs(mono) > peak * 0.1))
    x, mono = x[on:], mono[on:]
    env = np.abs(mono)
    above = np.where(env > peak * 10 ** (-40 / 20))[0]
    length_ms = (above[-1] if len(above) else 0) / sr * 1000
    hit = mono[: int(0.15 * sr)]

    low = band(mono, sr, 20, 400)
    f_settle = zc_freq(low[int(0.12 * sr): int(0.22 * sr)], sr) if len(low) > 0.22 * sr else zc_freq(low[int(0.06 * sr):], sr)
    f_early = zc_freq(low[int(0.003 * sr): int(0.012 * sr)], sr)
    # Sweep time: first window where the local frequency is within 20% of the settled pitch.
    sweep_ms = ""
    if f_settle > 20:
        w = int(0.01 * sr)
        for s in range(0, int(0.2 * sr), int(0.002 * sr)):
            fz = zc_freq(low[s: s + w * 2], sr)
            if fz and fz < f_settle * 1.2:
                sweep_ms = round(s / sr * 1000, 1)
                break

    seg = x[: int(min(len(x), 0.6 * sr))]
    mid, side = seg.mean(axis=1), (seg[:, 0] - seg[:, 1]) / 2
    res = {
        "file": os.path.relpath(path, os.environ.get("PK_ROOT", "/")),
        "sr": sr, "len_ms": round(length_ms), "peak_db": round(db(peak), 1),
        "hit_rms_db": round(db(rms(hit)), 1), "crest_db": round(db(peak) - db(rms(hit)), 1),
        "f_settle": round(f_settle, 1), "f_early": round(f_early), "sweep_ms": sweep_ms,
        "tail_rel_db": round(db(rms(mono[int(0.15 * sr): int(0.4 * sr)])) - db(rms(hit)), 1) if len(mono) > 0.2 * sr else "",
        "identical_lr": bool(np.max(np.abs(x[:, 0] - x[:, 1])) < 1e-6),
    }
    tot = rms(mid) ** 2 + 1e-20
    for lo, hi in [(20, 60), (60, 120), (120, 300), (300, 1000), (1000, 3000), (3000, 8000), (8000, 20000)]:
        res["e_%d" % lo] = round(10 * np.log10(rms(band(mid, sr, lo, hi)) ** 2 / tot + 1e-12), 1)
    for name, lo, hi in [("sub", 20, 120), ("body", 120, 1000), ("high", 1000, 20000)]:
        m, s = band(mid, sr, lo, hi), band(side, sr, lo, hi)
        res["side_" + name] = round(db(rms(s)) - db(rms(m)), 1)
    hl, hr = band(seg[:, 0], sr, 1000, 20000), band(seg[:, 1], sr, 1000, 20000)
    denom = np.sqrt(np.sum(hl * hl) * np.sum(hr * hr)) + 1e-20
    res["corr_high"] = round(float(np.sum(hl * hr) / denom), 3)
    # Inter-channel lag of the highs (positive = right later), +-20 ms.
    max_lag = int(0.02 * sr)
    n = len(hl)
    c = np.fft.irfft(np.fft.rfft(hl, 2 * n) * np.conj(np.fft.rfft(hr, 2 * n)), 2 * n)
    c = np.concatenate([c[-max_lag:], c[: max_lag + 1]])
    res["lag_high_ms"] = round((max_lag - int(np.argmax(c))) / sr * 1000, 2)
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
    for f in files:
        try:
            r = analyze(f)
            if r:
                rows.append(r)
        except Exception as e:  # noqa: BLE001 - report and continue
            print("skip", f, e, file=sys.stderr)
    with open(out, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print(len(rows), "files")


if __name__ == "__main__":
    main()
