"""Подбор фильтра G для центра C = x - (1 - cos t) * G(x): близко к прежнему |L + cos t * H| (LR4 на SPLIT),
без фазового сдвига низа. Частоты в долях SPLIT; theta = 0.65 * WIDTH."""
import sys, os
sys.path.insert(0, os.path.expanduser("~/Library/Caches/PowerKickTools/py"))
import numpy as np
w = np.logspace(np.log10(0.1), np.log10(16), 2000)
s = 1j * w
bw2 = lambda s, q: 1 / (s**2 + s / q + 1)
LP2 = bw2(s, 0.7071); HP2 = s**2 * bw2(s, 0.7071)
L, H = LP2**2, HP2**2                      # LR4
# side: LR4 high -> 8th-order Butterworth HP (|.| only, decorrelated)
q8 = [0.5098, 0.6013, 0.9000, 2.5629]
HP8 = np.prod([s**2 * bw2(s, q) for q in q8], axis=0)
def report(name, G, width=1.0):
    t = 0.65 * width; k = 1 - np.cos(t)
    old_c = L + np.cos(t) * H
    new_c = 1 - k * G
    side = np.sin(t) * np.abs(H * HP8)
    old_p = 10 * np.log10(np.abs(old_c)**2 + side**2); new_p = 10 * np.log10(np.abs(new_c)**2 + side**2)
    dmid = 20 * np.log10(np.abs(new_c) / np.abs(old_c))
    band = (w >= 0.25) & (w <= 8)
    low = w <= 0.5
    phase_low = np.degrees(np.angle(new_c[low]))
    gd = -np.gradient(np.unwrap(np.angle(new_c)), w)  # in units of 1/omega_c
    i60 = np.argmin(np.abs(w - 0.4))
    print("%-34s mid vs old: max %+.2f / min %+.2f dB; power vs old max %+.2f dB; phase<=0.5fc max %.1f deg; gd@0.4fc %.3f (old %.3f)" % (
        name, dmid[band].max(), dmid[band].min(), (new_p - old_p)[band].max(), np.abs(phase_low).max(), gd[i60],
        (-np.gradient(np.unwrap(np.angle(old_c)), w))[i60]))
report("LR4 high (сейчас в коде)", H)
for m in (0.7, 0.85, 1.0, 1.2, 1.4):
    sm = s / m
    report("HP2 Butterworth x%.2f" % m, (sm**2) * bw2(sm, 0.7071))
for m in (0.7, 1.0, 1.4):
    sm = s / m
    report("HP1 x%.2f" % m, sm / (sm + 1))
for q in (0.5, 0.6):
    for m in (1.0, 1.2):
        sm = s / m
        report("HP2 Q%.2f x%.2f" % (q, m), (sm**2) * bw2(sm, q))
print("---- уточнение HP1")
for m in (0.75, 0.8, 0.85, 0.9, 1.0):
    sm = s / m
    for wd in (1.0, 0.5):
        report("HP1 x%.2f WIDTH %.1f" % (m, wd), sm / (sm + 1), wd)
