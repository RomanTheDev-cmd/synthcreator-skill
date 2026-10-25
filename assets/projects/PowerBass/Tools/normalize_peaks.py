"""Sets every factory preset's MASTER so the louder channel peaks at -1 dBFS over the level grid, and no
note or roll of it reaches the emergency ceiling (0.9 linear, -0.915 dBFS). A resonant filter peaks differently on every
note and, through its envelope, at every tempo, so the grid covers both:
  - both test riffs (rolling 16ths, legato 8ths) at 150 BPM on 5 roots, E1..E3;
  - a held note on every semitone E1..C3;
  - rolling 16ths at 130 and 170 BPM and rolling 32nds at 130, 150 and 170 BPM on the same 5 roots
    (the tempo range of techno, hard techno and schranz).
Run, rebuild, run again until stable. PB_THREADS sets the number of render servers (default 8)."""
import os, re, sys
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
if os.environ.get("PK_NUMPY_PATH"): sys.path.insert(0, os.environ["PK_NUMPY_PATH"])
import numpy as np
from fit_bass import Renderer, load_presets

ROOTS = (28, 33, 38, 43, 48)
# (root, pattern, bpm); a 32nd roll at T BPM is the 16th roll at 2T.
JOBS = ([(root, pat, 150) for root in ROOTS for pat in (0, 1)] + [(root, 2, 150) for root in range(28, 49)]
        + [(root, 0, bpm) for root in ROOTS for bpm in (130, 170, 260, 300, 340)])


def measure(chunk):
    """Loudest peak (dBFS at MASTER -40) of each preset in the chunk, on its own render server."""
    r = Renderer()
    try:
        # MASTER is the last, linear gain: measure far below the emergency ceiling, then solve exactly.
        return {name: max(20 * np.log10(np.max(np.abs(r.render({**v, "master": -40.0}, root, pat, bpm))))
                          for root, pat, bpm in JOBS)
                for name, cat, v in chunk}
    finally:
        r.p.kill()


presets = load_presets()
threads = max(1, int(os.environ.get("PB_THREADS", 8)))
peaks = {}
with ThreadPoolExecutor(threads) as ex:
    for part in ex.map(measure, [presets[i::threads] for i in range(threads)]):
        peaks.update(part)

p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Source", "FactoryPresets.inc")
s = open(p).read()
worst = 0.0
for name, cat, v in presets:
    pk = peaks[name]
    # MASTER moves in 0.1 dB steps in the plug-in: round down, so the peak lands in (-1.1, -1.0] dBFS.
    new = max(-30.0, min(6.0, np.floor((-40.0 + (-1.0 - pk)) * 10.0 + 1e-6) / 10.0))
    worst = max(worst, abs(new - v.get("master", -6.0)))
    line = re.search(r'\{ "%s", "[^"]+", \{.*\} \},.*' % re.escape(name), s).group(0)
    if '{"master",' in line:
        nl = re.sub(r'\{"master",-?[\d.e+-]+f?\}', '{"master",%.1ff}' % new, line)
    else:
        nl = line.replace(' } },', ',{"master",%.1ff} } },' % new, 1)
    s = s.replace(line, nl)
open(p, "w").write(s)
print("largest master change: %.2f dB" % worst)
