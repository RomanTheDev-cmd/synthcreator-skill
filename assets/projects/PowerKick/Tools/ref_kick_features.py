"""Computes kick_features for every reference one-shot listed in Референсы/shots.csv
and assigns each to a PowerKick category by its pack/folder. Output: Референсы/kick_features.pkl"""
import csv, os, pickle, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
if os.environ.get("PK_NUMPY_PATH"):
    sys.path.insert(0, os.environ["PK_NUMPY_PATH"])
from wavread import read_wav
from kick_features import features

ROOT = os.path.expanduser("~/Library/Mobile Documents/com~apple~CloudDocs/definitionofhardtechno")
RULES = [  # (substring of folder, category)
    ("OBI Punchy Kicks", "Techno"), ("Simple Kicks", "Techno"),
    ("ARK Kick Drum Shots", "Schranz"), ("SSP Signature Schranz Kick", "Schranz"), ("02 Signature Kick Shots", "Schranz"),
    ("ITHTS Signature Schanz", "Schranz"), ("OBI Driven Schranz", "Schranz"),
    ("HDIT3 Heavy", "Industrial"), ("HDIT3 Assorted", "Industrial"), ("HDIT3 Processed FX", "Industrial"),
    ("ITHTS Heavy Kick Shots", "Industrial"), ("ITHTS Distorted", "Industrial"), ("OBI Industrial", "Industrial"),
    ("HST1 Rumble Kick Shots", "Hard Techno"), ("HST1 Dirty Kick Shots", "Hard Techno"), ("HST1 Short", "Hard Techno"),
    ("SVE2 Kick Shots", "Hard Techno"), ("SVE2 Rumble Shots", "Hard Techno"), ("OBI Hard Rumble", "Hard Techno"),
    ("BORIS", "Hard Techno"), ("Complex Kicks", "Hard Techno"), ("ITHTS Rumble Kick Shots", "Hard Techno"),
]
here = os.path.dirname(os.path.abspath(__file__))
rows = list(csv.DictReader(open(os.path.join(here, "..", "Референсы", "shots.csv"))))
out = []
for r in rows:
    cat = next((c for key, c in RULES if key in r["file"]), None)
    if cat is None:
        continue
    x, sr = read_wav(os.path.join(ROOT, r["file"]))
    f = features(x, sr)
    if f is not None:
        out.append({"file": r["file"], "category": cat, "feat": f})
pickle.dump(out, open(os.path.join(here, "..", "Референсы", "kick_features.pkl"), "wb"))
from collections import Counter
print(len(out), Counter(o["category"] for o in out))
