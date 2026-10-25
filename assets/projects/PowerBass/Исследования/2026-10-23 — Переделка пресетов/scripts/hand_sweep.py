"""Б5: подстройка яркости ручных вариантов (срез, TONE, BITE) к медиане жанра Dark Underground."""
import json, os, sys, itertools
TOOLS, BIN = sys.argv[1:3]
sys.path.insert(0, os.path.expanduser("~/Library/Caches/PowerKickTools/py")); sys.path.insert(0, TOOLS)
os.environ.setdefault("PK_REFS", "bass_features_v3.pkl")
import numpy as np
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "library_report.py")).read()
exec(src.split("rows = []")[0].replace("TOOLS, BIN, OUT = sys.argv[1:4]", "").replace('"bass_features_v2.pkl"', '"bass_features_v3.pkl"'))
D = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "hand_designs.json")))
base = dict(glide=0.0, glideMode=1, splitFreq=150.0, filterPos=0, fmAmount=0.0, fmRatio=5, fmDecay=200.0, oscMix=0.0, velSens=0.5, master=-12.0)
items = []
for name in ("Reese Core A (жёсткий риз)", "Reese Core B (тёмный грязный)", "Dark Pulse A (квадрат с щелчком фильтра)", "Dark Pulse B (пила-квадрат, клип)"):
    cuts = (1400.0, 1900.0, 2600.0) if name.startswith("Reese") else (750.0, 1000.0, 1400.0)
    for c, t, b in itertools.product(cuts, (0.7, 0.85), (0.45,)):
        items.append(("%s | cut %.0f tone %.2f" % (name, c, t), "Dark Underground Bass", {**base, **D[name], "cutoff": c, "distTone": t, "bite": b}))
rows = work(items)
for r in rows: print("%-62s >24f %6.1f | >6f %6.1f | скачок %5.1f | пауза %6.1f" % (r["name"], r["bright24"], r["bright6"], r["jump_db"], r["gap_db"]))
