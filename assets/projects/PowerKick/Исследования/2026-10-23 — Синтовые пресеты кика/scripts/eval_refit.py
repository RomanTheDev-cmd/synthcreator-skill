"""К3: тональность середины и звон переподобранных пресетов против референсов своего жанра (до и после).
  python3 eval_refit.py <PowerKick/Tools> <refit.json>   (PK_TESTS — сервер рендера)"""
import json, os, pickle, sys, collections
TOOLS, FIT = sys.argv[1:3]
sys.path.insert(0, os.path.expanduser("~/Library/Caches/PowerKickTools/py")); sys.path.insert(0, TOOLS)
import numpy as np
import fit_kicks as fk, refit_kicks as rk
from kick_features import BAND_EDGES
hi = BAND_EDGES[:-1] >= 300
refs = pickle.load(open(os.path.join(TOOLS, "..", "Референсы", "kick_features_v2.pkl"), "rb"))
G = collections.defaultdict(list)
for r in refs: G[r["category"]].append((r["feat"]["tonal"][1], float(np.max(r["feat"]["spec"][2][hi]))))
lim = {c: (np.percentile([t for t, _ in v], 10), np.percentile([g for _, g in v], 95)) for c, v in G.items()}
lib = {e["name"]: e for e in rk.library()}
r = fk.Renderer(); rows = []; bad = collections.Counter()
for f in json.load(open(FIT, encoding="utf-8")):
    c = f["category"]; t10, g95 = lim[c]
    a = r.feat({**lib[f["name"]]["params"], "output": fk.FIT_OUTPUT}); b = r.feat({**f["params"], "output": fk.FIT_OUTPUT})
    ga, gb = float(np.max(a["spec"][2][hi])), float(np.max(b["spec"][2][hi]))
    okA = a["tonal"][1] >= t10 and ga <= g95; okB = b["tonal"][1] >= t10 and gb <= g95
    bad["было вне нормы"] += not okA; bad["стало вне нормы"] += not okB
    rows.append("%-24s %-12s тональность %6.1f → %6.1f (10%% жанра %6.1f) | звон %6.1f → %6.1f (95%% %6.1f) | %s → %s | расстояние %.1f → %.1f"
                % (f["name"], c, a["tonal"][1], b["tonal"][1], t10, ga, gb, g95, "норма" if okA else "ВНЕ", "норма" if okB else "ВНЕ", f["before"], f["distance"]))
r.p.kill()
print("\n".join(rows)); print(dict(bad), "из", len(rows))
