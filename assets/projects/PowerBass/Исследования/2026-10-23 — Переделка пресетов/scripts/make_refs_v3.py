"""Б2: набор референсов v3 (только числа): «Raw Analog Bass» — басовые синт-линии вместо саб-слоёв SVEBassLoop,
«Rolling Techno Bass» — без рамбл-лупов. Остальные жанры как в v2."""
import os, sys, pickle, re
TOOLS, CAND = sys.argv[1], sys.argv[2]
sys.path.insert(0, os.path.expanduser("~/Library/Caches/PowerKickTools/py")); sys.path.insert(0, TOOLS)
import numpy as np
from fit_bass import kmedoids
refs = pickle.load(open(os.path.join(TOOLS, "..", "Референсы", "bass_features_v2.pkl"), "rb"))
cand = pickle.load(open(CAND, "rb"))
bad = re.compile(r"supersaw|trance|wet|lead", re.I)
pick = [c for c in cand if c["f0"] <= 110 and -20 <= c["b6"] <= -2 and not bad.search(c["file"])]
if len(pick) > 30:
    med, _ = kmedoids([c["feat"] for c in pick], 30)
    pick = [pick[i] for i in med]
out = [r for r in refs if r["category"] != "Raw Analog Bass" and not (r["category"] == "Rolling Techno Bass" and "Rumble" in r["file"])]
out += [{"file": c["file"], "category": "Raw Analog Bass", "feat": c["feat"]} for c in pick]
pickle.dump(out, open(os.path.join(TOOLS, "..", "Референсы", "bass_features_v3.pkl"), "wb"))
from collections import Counter
print("v3:", len(out), dict(Counter(r["category"] for r in out)))
print("новые «Raw Analog»:", len(pick)); [print("   ", c["file"][-75:]) for c in pick]
