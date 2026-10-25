"""Average signed residuals (fitted - target) over fitted presets: shows what the engine cannot do."""
import json, os, pickle, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
if os.environ.get("PK_NUMPY_PATH"): sys.path.insert(0, os.environ["PK_NUMPY_PATH"])
import numpy as np
from fit_kicks import Renderer, load_refs
from kick_features import BAND_EDGES, SEGMENTS, VALID, distance
fits = json.load(open(sys.argv[1]))
refs = {x["file"]: x["feat"] for x in load_refs()}
r = Renderer()
env_d, spec_d, f0_d = [], [], []
for m in fits:
    if m["distance"] > 18: continue
    a, b = r.feat({**m["params"], "output": -12.0}), refs[m["medoid"]]
    env_d.append(a["env"] - b["env"]); spec_d.append(a["spec"] - b["spec"])
    both = ~np.isnan(a["f0"]) & ~np.isnan(b["f0"]); f0_d.append(np.where(both, a["f0"] - b["f0"], np.nan))
env_d, spec_d, f0_d = np.array(env_d), np.array(spec_d), np.array(f0_d)
print("ENV signed residual (dB), mean over fits, by time:")
for a, b in [(0, 2), (2, 6), (6, 12), (12, 24), (24, 40), (40, 60), (60, 80), (80, 100), (100, 120)]:
    print("  %3d-%3d ms  %+5.1f  (abs %4.1f)" % (a * 5, b * 5, np.mean(env_d[:, a:b]), np.mean(np.abs(env_d[:, a:b]))))
print("SPEC signed residual (dB): rows = segments, cols = bands")
print("           " + " ".join("%6d" % int(round(f)) for f in np.sqrt(BAND_EDGES[:-1] * BAND_EDGES[1:])))
for i, (a, b) in enumerate(SEGMENTS):
    print("  %3d-%3dms " % (a * 1000, b * 1000) + " ".join(("%+6.1f" % v) if ok else "     ." for v, ok in zip(np.mean(spec_d[:, i, :], axis=0), VALID[i])))
print("  abs mean  " + " ".join("%6.1f" % v for v in np.mean(np.abs(spec_d), axis=(0, 1))))
print("F0 signed residual (st) by time:", " ".join("%+.1f" % np.nanmean(f0_d[:, i:i + 10]) for i in range(0, f0_d.shape[1], 10)))
r.p.kill()
