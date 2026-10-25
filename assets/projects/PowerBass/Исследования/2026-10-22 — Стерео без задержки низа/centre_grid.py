import sys, os
sys.path.insert(0, os.path.expanduser("~/Library/Caches/PowerKickTools/py"))
import numpy as np
w = np.logspace(-1.3, np.log10(16), 3000); s = 1j * w
bw2 = lambda s, q: 1 / (s**2 + s / q + 1)
L, H = (bw2(s, .7071))**2, (s**2 * bw2(s, .7071))**2
res = []
def ev(name, G):
    worst = 0; lowd = 0; gd = 0
    for wd in (1.0, 0.5):
        t = .65 * wd; k = 1 - np.cos(t)
        old = np.abs(L + np.cos(t) * H); new = 1 - k * G
        d = 20 * np.log10(np.abs(new) / old)
        worst = max(worst, np.abs(d).max())
        # test: mid below 0.5 fc vs WIDTH 0 (new: x itself)
        lowd = max(lowd, np.abs(20 * np.log10(np.abs(new[w <= 0.5]))).max())
        gdv = -np.gradient(np.unwrap(np.angle(new)), w); gd = max(gd, gdv[np.argmin(np.abs(w - .4))])
    res.append((worst, lowd, gd, name))
for m in np.arange(0.8, 1.6, 0.05):
    sm = s / m; ev("HP1 x%.2f" % m, sm / (sm + 1))
for q in (0.5, 0.55, 0.6, 0.65, 0.7071):
    for m in np.arange(0.5, 1.3, 0.05):
        sm = s / m; ev("HP2 Q%.2f x%.2f" % (q, m), sm**2 * bw2(sm, q))
res.sort()
print("лучшие по наибольшему отличию от прежнего центра (WIDTH 1 и 0.5):")
for r in res[:12]: print("  %-18s отличие %.2f дБ; низ ниже 0.5 SPLIT %.2f дБ; задержка@0.4 %.3f/wc" % (r[3], r[0], r[1], r[2]))
print("лучшие при низе ниже 0.5 SPLIT <= 0.25 дБ:")
for r in [r for r in res if r[1] <= 0.25][:8]: print("  %-18s отличие %.2f дБ; низ %.2f дБ; задержка %.3f" % (r[3], r[0], r[1], r[2]))
