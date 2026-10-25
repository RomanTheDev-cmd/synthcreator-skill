"""Начало нот в партии (ролл 1/16, gate 75 %, 133 BPM): «Bass 2» из проекта и варианты."""
import re, subprocess, sys, os
sys.path.insert(0, os.path.expanduser("~/Library/Caches/PowerKickTools/py"))
import numpy as np
BIN, XML = sys.argv[1], sys.argv[2]
SR, BPM, LEN = 48000, 133.0, 6.0
B = {k: float(v) for k, v in re.findall(r'id="([^"]+)" value="([^"]+)"', open(XML).read())}
p = subprocess.Popen([BIN, "--serve"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
def render(v, root):
    p.stdin.write((" ".join("%s=%g" % kv for kv in v.items()) + " _root=%d _bpm=%g _len=%g _pattern=0\n" % (root, BPM, LEN)).encode()); p.stdin.flush()
    n = int(LEN * SR) * 8; buf = b""
    while len(buf) < n: buf += p.stdout.read(n - len(buf))
    return np.frombuffer(buf, dtype="<f4").reshape(-1, 2).mean(1).astype(np.float64)
def band(sig, lo, hi):
    F = np.fft.rfft(sig); f = np.fft.rfftfreq(len(sig), 1 / SR); F[(f < lo) | (f > hi)] = 0; return np.fft.irfft(F, len(sig))
def envdb(sig, w_ms):
    w = int(SR * w_ms / 1000); return 20 * np.log10(np.sqrt(np.convolve(sig * sig, np.ones(w) / w, "same")) + 1e-9)
step = 60 / BPM / 4
def per_note(x):
    e = envdb(band(x, 40, 1500), 4); res = []
    for k in range(1, int(LEN / step) - 2):
        if k % 4 == 0: continue
        i0 = int(k * step * SR); seg = e[i0: i0 + int(0.75 * step * SR)]
        res.append((seg[int(0.005 * SR)] - seg.max(), np.argmax(seg >= seg.max() - 1) / SR * 1000))
    return np.array(res)
variants = [("Bass 2 как в проекте", B), ("RELEASE 5 мс", {**B, "release": 5.0}), ("RELEASE 30 мс", {**B, "release": 30.0}),
            ("DETUNE 0", {**B, "detune": 0.0}), ("огибающая фильтра 0", {**B, "envAmount": 0.0})]
for root in (28, 33, 40):
    print("нота %d:" % root)
    for nm, v in variants:
        r = per_note(render(v, root))
        print("   %-22s уровень через 5 мс от ноты относительно её максимума: медиана %5.1f дБ, худшие 10%% %5.1f дБ | до −1 дБ: медиана %5.1f мс, худшие 10%% %5.1f мс" % (
            nm, np.median(r[:, 0]), np.percentile(r[:, 0], 10), np.median(r[:, 1]), np.percentile(r[:, 1], 90)))
p.kill()
