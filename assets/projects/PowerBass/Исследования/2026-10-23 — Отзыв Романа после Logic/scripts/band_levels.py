"""Абсолютные уровни по полосам во времени: заводской Biting Reese 02 и его варианты."""
import re, subprocess, sys, os
sys.path.insert(0, os.path.expanduser("~/Library/Caches/PowerKickTools/py"))
import numpy as np
BIN, D = sys.argv[1], sys.argv[2]
SR = 48000
st = lambda f: {k: float(v) for k, v in re.findall(r'id="([^"]+)" value="([^"]+)"', open(os.path.join(D, f)).read())}
A, B = st("state_31693_jucePluginState.xml"), st("state_49108_jucePluginState.xml")
p = subprocess.Popen([BIN, "--serve"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
def render(v, root, length=1.0):
    p.stdin.write((" ".join("%s=%g" % kv for kv in v.items()) + " _root=%d _bpm=133 _len=%g _pattern=2\n" % (root, length)).encode()); p.stdin.flush()
    n = int(length * SR) * 8; buf = b""
    while len(buf) < n: buf += p.stdout.read(n - len(buf))
    return np.frombuffer(buf, dtype="<f4").reshape(-1, 2).mean(1).astype(np.float64)
def band(sig, lo, hi):
    F = np.fft.rfft(sig); f = np.fft.rfftfreq(len(sig), 1 / SR); F[(f < lo) | (f > hi)] = 0; return np.fft.irfft(F, len(sig))
T = (3, 10, 25, 50, 100, 200, 400)
def lv(sig, w_ms):
    w = int(SR * w_ms / 1000); e = np.sqrt(np.convolve(sig * sig, np.ones(w) / w, "same"))
    return [20 * np.log10(e[int(t / 1000 * SR)] + 1e-9) for t in T]
variants = [("A первый бас", A), ("B заводской", B), ("B без огиб. фильтра", {**B, "envAmount": 0.0}),
            ("B резонанс 0", {**B, "resonance": 0.0}), ("B без огиб., рез. 0", {**B, "envAmount": 0.0, "resonance": 0.0})]
for root in (33, 40):
    print("нота %d; уровни дБ в моменты %s мс" % (root, ", ".join(map(str, T))))
    for nm, v in variants:
        x = render(v, root)
        lo = lv(band(x, 20, 120), 12); hi = lv(band(x, 600, 6000), 3); al = lv(x, 6)
        print("  %-20s низ: %s | верх: %s | всё: %s" % (nm, " ".join("%5.1f" % a for a in lo), " ".join("%5.1f" % a for a in hi), " ".join("%5.1f" % a for a in al)))
p.kill()
