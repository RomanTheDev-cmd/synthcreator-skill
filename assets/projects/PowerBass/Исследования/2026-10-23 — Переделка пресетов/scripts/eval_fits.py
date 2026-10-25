"""Проверка переподобранных пресетов (JSON от refit_bass.py) против прежних: рамки, яркость против жанра (v3),
пауза на долю кика и скачок в начале нот на роллинговой партии, расстояние до референса."""
import json, os, sys, re, math
TOOLS, BIN, FIT = sys.argv[1:4]
sys.path.insert(0, os.path.expanduser("~/Library/Caches/PowerKickTools/py")); sys.path.insert(0, TOOLS)
os.environ.setdefault("PK_REFS", "bass_features_v3.pkl")
import numpy as np
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "library_report.py")).read()
src = src.split("rows = []")[0].replace("TOOLS, BIN, OUT = sys.argv[1:4]", "").replace('"bass_features_v2.pkl"', '"bass_features_v3.pkl"')
exec(src)
import refit_bass as rb
old = {e["name"]: e for e in rb.library()}
fits = json.load(open(FIT))
old_dist = {}
for line in open(rb.INC, encoding="utf-8"):
    m = re.search(r'^\{ "([^"]+)".*// fit ([\d.]+) to', line)
    if m: old_dist[m.group(1)] = float(m.group(2))
items_new = [(f["name"], f["category"], {**f["params"], "master": old[f["name"]]["params"].get("master", -12)}) for f in fits]
items_old = [(f["name"], f["category"], old[f["name"]]["params"]) for f in fits]
new_rows = {r["name"]: r for r in work(items_new)}; old_rows = {r["name"]: r for r in work(items_old)}
for f in fits:
    n = f["name"]; a, b = old_rows[n], new_rows[n]; rbB = refB[f["category"]]; p = f["params"]
    ok = p["release"] <= 150.01 and p["distTone"] >= 0.399 and p["dark"] <= 0.601 and p["cutoff"] >= 149.9
    print("%-17s рамки %s | яркость >24f %6.1f → %6.1f (10%% жанра %6.1f) | пауза %6.1f → %6.1f дБ | скачок %5.1f → %5.1f дБ | расстояние %.1f → %.1f | RELEASE %.0f → %.0f" % (
        n, "да" if ok else "НЕТ", a["bright24"], b["bright24"], np.percentile(rbB[:, 1], 10), a["gap_db"], b["gap_db"], a["jump_db"], b["jump_db"], old_dist.get(n, float("nan")), f["distance"], a["release"], b["release"]))
