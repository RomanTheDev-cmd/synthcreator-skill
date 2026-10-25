"""Sets every factory preset's OUTPUT so the louder channel's TRUE peak is -1 dBTP (headroom for the mix;
the emergency ceiling at -0.9 dBFS is never reached), over single hits at three velocities and rolls on
16ths and 32nds (also accented, 1.0 / 0.8). True peak since 2026-10-24 (4x windowed-sinc, as truePeak() in the tests):
a sample peak of -1 dBFS left up to +0.86 dBTP between the samples.
Run, rebuild, run again until stable."""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
if os.environ.get("PK_NUMPY_PATH"): sys.path.insert(0, os.environ["PK_NUMPY_PATH"])
import numpy as np
from fit_kicks import Renderer, load_presets
r = Renderer()


def true_peak(x):
    """Largest |value| of the samples and of 3 points between each pair (4x windowed-sinc, Blackman, 64 taps)."""
    k = np.arange(64)
    best = float(np.max(np.abs(x)))
    for ch in range(x.shape[1]):
        s = x[:, ch].astype(np.float64); mx = np.max(np.abs(s))
        idx = np.where((np.abs(s[31:-33]) >= 0.5 * mx) | (np.abs(s[32:-32]) >= 0.5 * mx))[0] + 31
        if len(idx) == 0:
            continue
        win = s[idx[:, None] - 31 + k[None, :]]
        for f in (0.25, 0.5, 0.75):
            d = (k - 31) - f; a = np.pi * d
            h = np.sinc(d) * (0.42 + 0.5 * np.cos(a / 32) + 0.08 * np.cos(2 * a / 32))
            best = max(best, float(np.max(np.abs(win @ h))))
    return best


def render_len(params, extra, length):
    """Like Renderer.render, with its own length (rolls need more than the 0.6 s of a single hit)."""
    line = " ".join("%s=%g" % kv for kv in {**params, **extra}.items()) + " _len=%g _sr=48000\n" % length
    r.p.stdin.write(line.encode())
    r.p.stdin.flush()
    n = int(length * 48000) * 2 * 4
    buf = b""
    while len(buf) < n:
        buf += r.p.stdout.read(n - len(buf))
    return np.frombuffer(buf, dtype="<f4").reshape(-1, 2)
p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Source", "Presets.cpp")
s = open(p).read()
worst = 0.0
for name, cat, v in load_presets():
    # OUTPUT is linear below the emergency ceiling: probe 24 dB lower and solve exactly. The level
    # comes from the loudest of a single hit and rolls on 16ths and 32nds at 150 BPM, so the
    # emergency ceiling does not shape rolls either.
    probe = {**v, "output": v["output"] - 24.0}
    # Softer hits can peak higher (velocity lowers the click more than the body, and the drive
    # reshapes the sum), so single hits at three velocities and accented rolls are included.
    cases = [({"_vel": vel}, 0.6) for vel in (1.0, 0.85, 0.7)]
    for step in (4800, 2400):
        cases += [({"_step": step, "_bpm": 150}, 2.0), ({"_step": step, "_bpm": 150, "_vel": 0.8, "_alt": 1}, 2.0)]
    # Four on the floor and 8ths: the tail of a hit is still there when the next one comes, which a single hit
    # and the cut-off rolls do not show (up to +1.3 dB higher; found by the independent audit, 2026-10-24).
    for step, bpm in ((22154, 130), (19200, 150), (16941, 170), (9600, 150)):
        cases.append(({"_step": step, "_bpm": bpm}, 2.0))
    pk = max(20 * np.log10(true_peak(render_len(probe, extra, length))) for extra, length in cases) + 24.0
    worst = max(worst, abs(pk + 1.0))
    m = re.search(r'(\{ "%s",\s+"[^"]+",\s+\{ )([^}]*)( \}(?:, "[^"]*")? \},)' % re.escape(name), s)
    vals = [x.strip() for x in m.group(2).split(",")]
    # OUTPUT moves in 0.1 dB steps in the plug-in: round down, so the peak lands in (-1.1, -1.0] dBFS
    # and never closer than 0.085 dB to the ceiling.
    vals[8] = "%.1ff" % max(-24.0, min(6.0, np.floor((v["output"] + (-1.0 - pk)) * 10.0 + 1e-6) / 10.0))
    s = s[:m.start(2)] + ", ".join(vals) + s[m.end(2):]
open(p, "w").write(s)
r.p.kill()
print("max deviation before this pass: %.2f dB" % worst)
