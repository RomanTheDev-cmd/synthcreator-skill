"""bass_features for every reference bass loop, with a PowerBass category from its folder.
Output: Референсы/bass_features.pkl (numbers only)."""
import os, pickle, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
if os.environ.get("PK_NUMPY_PATH"):
    sys.path.insert(0, os.environ["PK_NUMPY_PATH"])
from wavread import read_wav
from bass_features import features

ROOT = os.path.expanduser("~/Library/Mobile Documents/com~apple~CloudDocs/definitionofhardtechno")
RULES = [
    ("Reese Basslines", "Dark Underground Bass"), ("HDIT3 Basslines", "Dark Underground Bass"),
    ("Rumble Bass Loops", "Hard Techno Rumble Bass"),
    ("Signature Saw Basslines", "Schranz Distorted Bass"),
    ("ITHTS Synth Basslines", "Industrial FM Bass"),
    ("Wobble Bass", "Metallic Bass"),
    ("Hardtechno & Schranz Vol.1/Bass Loops", "Raw Analog Bass"), ("SVE2 Bass Loops", "Raw Analog Bass"),
    ("Dark Acid Lines", "Acid-Inspired Bass"),
    ("Running Basslines Clean", "Rolling Techno Bass"), ("Bonus Basslines", "Rolling Techno Bass"),
    ("Running Basslines Dirty", "Destroyed Bass"),
]
out = []
for dirpath, _, files in os.walk(ROOT):
    rel = os.path.relpath(dirpath, ROOT)
    if "DEMO" in rel or "MIDI" in rel:
        continue
    cat = next((c for key, c in RULES if key in rel), None)
    if cat is None:
        continue
    for name in sorted(files):
        if not name.lower().endswith(".wav"):
            continue
        try:
            x, sr = read_wav(os.path.join(dirpath, name))
            f = features(x, sr)
        except Exception as e:  # noqa: BLE001
            print("skip", name, e, file=sys.stderr)
            continue
        if f is not None:
            out.append({"file": os.path.join(rel, name), "category": cat, "feat": f})
pickle.dump(out, open(os.path.join(HERE, "..", "Референсы", "bass_features.pkl"), "wb"))
from collections import Counter
print(len(out), Counter(o["category"] for o in out))
