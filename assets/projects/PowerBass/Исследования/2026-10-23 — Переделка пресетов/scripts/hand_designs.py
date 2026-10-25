"""Б5: ручные варианты Reese Core и Dark Pulse — числа против жанра Dark Underground (v3)."""
import json, os, sys
TOOLS, BIN = sys.argv[1:3]
sys.path.insert(0, os.path.expanduser("~/Library/Caches/PowerKickTools/py")); sys.path.insert(0, TOOLS)
os.environ.setdefault("PK_REFS", "bass_features_v3.pkl"); os.environ["PB_TESTS"] = BIN
import numpy as np
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "library_report.py")).read()
exec(src.split("rows = []")[0].replace("TOOLS, BIN, OUT = sys.argv[1:4]", "").replace('"bass_features_v2.pkl"', '"bass_features_v3.pkl"'))
import fit_bass as fb, refit_bass as rb
base = dict(glide=0.0, glideMode=1, splitFreq=150.0, filterPos=0, fmAmount=0.0, fmRatio=5, fmDecay=200.0, oscMix=0.0, velSens=0.5, master=-12.0)
D = {
 "Reese Core A (жёсткий риз)": dict(shape=2.0, detune=0.6, sub=0.45, oscDrive=0.35, cutoff=1100.0, resonance=0.35, filterMode=0, envAmount=0.35, fltDecay=180.0,
     dark=0.15, bite=0.35, distType=1, distDrive=0.45, distTone=0.65, distMix=0.7, attack=1.0, decay=400.0, sustain=0.85, release=60.0, punch=0.25, width=0.55),
 "Reese Core B (тёмный грязный)": dict(shape=2.0, detune=0.7, sub=0.45, oscDrive=0.5, cutoff=750.0, resonance=0.4, filterMode=0, envAmount=0.3, fltDecay=220.0,
     dark=0.3, bite=0.45, distType=3, distDrive=0.6, distTone=0.6, distMix=0.6, attack=1.0, decay=500.0, sustain=0.85, release=60.0, punch=0.3, width=0.5),
 "Dark Pulse A (квадрат с щелчком фильтра)": dict(shape=3.0, detune=0.1, sub=0.4, oscDrive=0.3, cutoff=520.0, resonance=0.5, filterMode=0, envAmount=0.55, fltDecay=140.0,
     dark=0.35, bite=0.3, distType=0, distDrive=0.5, distTone=0.5, distMix=0.8, attack=1.0, decay=260.0, sustain=0.55, release=40.0, punch=0.4, width=0.25),
 "Dark Pulse B (пила-квадрат, клип)": dict(shape=2.6, detune=0.15, sub=0.4, oscDrive=0.25, cutoff=650.0, resonance=0.6, filterMode=0, envAmount=0.7, fltDecay=110.0,
     dark=0.3, bite=0.4, distType=1, distDrive=0.4, distTone=0.55, distMix=0.75, attack=1.0, decay=220.0, sustain=0.5, release=35.0, punch=0.35, width=0.3),
}
lib = {e["name"]: e for e in rb.library()}
items = [(n, "Dark Underground Bass", {**base, **p}) for n, p in D.items()] + [(n, "Dark Underground Bass", lib[n]["params"]) for n in ("Reese Core", "Dark Pulse")]
rows = {r["name"]: r for r in work(items)}
refs = fb.load_refs(); pool = [r for r in refs if r["category"] == "Dark Underground Bass"]
rend = fb.Renderer(); rbB = refB["Dark Underground Bass"]
print("жанр Dark Underground: яркость >24f медиана %.1f, 10%% %.1f, 90%% %.1f" % (np.median(rbB[:, 1]), np.percentile(rbB[:, 1], 10), np.percentile(rbB[:, 1], 90)))
for n, cat, p in items:
    dists = []
    for r in pool[::4]:
        root, pat = fb.setup(r["feat"]); f = rend.feat({**p, "master": -12.0}, root, pat)
        dists.append(fb.distance(f, r["feat"]) if f else 99)
    r = rows[n]
    print("%-42s >24f %6.1f | пауза %6.1f дБ | скачок %5.1f дБ | до ближайшего референса жанра %.1f" % (n, r["bright24"], r["gap_db"], r["jump_db"], min(dists)))
rend.p.kill()
json.dump(D, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "hand_designs.json"), "w"), ensure_ascii=False, indent=1)
