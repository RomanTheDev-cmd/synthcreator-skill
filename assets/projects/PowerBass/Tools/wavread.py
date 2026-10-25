"""Minimal WAV reader (PCM 16/24/32, float) returning stereo float64."""
import struct
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
