"""Нарастание громкости одной ноты PowerBass при разных ATTACK (состояния из проекта Logic)."""
import re, subprocess, sys, os
sys.path.insert(0, os.path.expanduser("~/Library/Caches/PowerKickTools/py"))
import numpy as np
BIN, D = sys.argv[1], sys.argv[2]
SR = 48000
def state(f):
    return {k: float(v) for k, v in re.findall(r'id="([^"]+)" value="([^"]+)"', open(f).read())}
A = state(os.path.join(D, "state_31693_jucePluginState.xml"))
B = state(os.path.join(D, "state_49108_jucePluginState.xml"))
p = subprocess.Popen([BIN, "--serve"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
def render(v, root=40, pattern=2, length=1.0, bpm=133):
    line = " ".join("%s=%g" % kv for kv in v.items()) + " _root=%d _bpm=%g _len=%g _pattern=%d\n" % (root, bpm, length, pattern)
    p.stdin.write(line.encode()); p.stdin.flush()
    n = int(length * SR) * 2 * 4; buf = b""
    while len(buf) < n: buf += p.stdout.read(n - len(buf))
    return np.frombuffer(buf, dtype="<f4").reshape(-1, 2)
def env(x, win_ms=3):
    m = x.mean(1); w = int(SR * win_ms / 1000)
    e = np.sqrt(np.convolve(m * m, np.ones(w) / w, mode="same"))
    return e
def report(name, v):
    x = render(v); e = env(x); pk = e[: int(0.5 * SR)].max()
    t = lambda db: np.argmax(e >= pk * 10 ** (db / 20)) / SR * 1000
    first = np.argmax(np.abs(x).max(1) > 1e-4) / SR * 1000
    print("%-34s первый звук %5.1f мс | до −20 дБ %5.1f | −6 дБ %5.1f | −1 дБ %5.1f мс | пик в %5.1f мс" % (name, first, t(-20), t(-6), t(-1), np.argmax(e[: int(0.5*SR)]) / SR * 1000))
for nm, v in (("A (первый, подкручен)", A), ("B (заводской Biting Reese 02)", B)):
    for at in (v["attack"], 20.0, 100.0, 300.0):
        report("%s ATTACK=%g" % (nm, at), {**v, "attack": at})
p.kill()
