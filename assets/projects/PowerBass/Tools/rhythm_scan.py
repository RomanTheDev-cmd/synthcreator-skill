"""Onset density of every reference bass loop: rolling lines are dense, short repeated notes.
Writes Референсы/bass_rhythm.json {file: {"onsets_per_s", "gap_fraction"}} (numbers only)."""
import json, os, pickle, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
if os.environ.get("PK_NUMPY_PATH"): sys.path.insert(0, os.environ["PK_NUMPY_PATH"])
import numpy as np
from wavread import read_wav
ROOT = os.path.expanduser("~/Library/Mobile Documents/com~apple~CloudDocs/definitionofhardtechno")
refs = pickle.load(open(os.path.join(HERE, "..", "Референсы", "bass_features.pkl"), "rb"))
out = {}
for x in refs:
    s, sr = read_wav(os.path.join(ROOT, x["file"]))
    m = s[: int(sr * 8)].mean(axis=1)
    hop = int(0.005 * sr)
    env = np.array([np.sqrt(np.mean(m[i:i + hop] ** 2)) for i in range(0, len(m) - hop, hop)])
    db = 20 * np.log10(env + 1e-9)
    # onsets: rises of >= 6 dB within 20 ms above a -30 dB floor (re loudest frame)
    floor = db.max() - 30
    rise = db[4:] - db[:-4]
    on = np.where((rise > 6) & (db[4:] > floor))[0]
    onsets = []
    for i in on:
        if not onsets or i - onsets[-1] > 12:   # >= 60 ms apart
            onsets.append(i)
    dur = len(m) / sr
    out[x["file"]] = {"onsets_per_s": len(onsets) / dur, "gap_fraction": float(np.mean(db < db.max() - 15)),
                      "category": x["category"]}
json.dump(out, open(os.path.join(HERE, "..", "Референсы", "bass_rhythm.json"), "w"), indent=0, ensure_ascii=False)
from collections import defaultdict
g = defaultdict(list)
for f, v in out.items(): g[v["category"]].append(v["onsets_per_s"])
for c, v in sorted(g.items()):
    v = np.array(v); print("%-26s n=%3d onsets/s median %.1f  >=5/s: %d" % (c, len(v), np.median(v), int(np.sum(v >= 5))))
