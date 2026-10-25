"""Партия «Bass 2» из проекта (ноты вплотную) через движок: начало каждой ноты у настроек обоих басов."""
import json, re, subprocess, sys, os
sys.path.insert(0, os.path.expanduser("~/Library/Caches/PowerKickTools/py"))
import numpy as np
BIN, L, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
SR, BPM = 48000, 133.0
notes = [o for o in json.load(open(os.path.join(L, "notes.json"))) if o["chunk"] == 260714][0]["notes"]
tick = 60.0 / BPM / 960.0 * SR; lead = int(0.5 * SR); reg = 4 * 4 * 960
seq = []
for rep in range(2):
    for pos, pitch, vel, ln in notes:
        on = lead + int(round((pos - 38400 + rep * reg) * tick)); off = lead + int(round((pos - 38400 + rep * reg + ln) * tick))
        seq.append((on, off, pitch, vel / 127.0))
open(os.path.join(OUT, "notes.txt"), "w").write("\n".join("%d %d %d %.4f" % n for n in seq))
def params(xml, **over):
    v = {k: float(x) for k, x in re.findall(r'id="([^"]+)" value="([^"]+)"', open(xml).read())}; v.update(over)
    f = os.path.join(OUT, "params.txt"); open(f, "w").write(" ".join("%s=%g" % kv for kv in v.items())); return f
def render(pf, order, block=512):
    raw = subprocess.run([BIN, "--notes", pf, os.path.join(OUT, "notes.txt"), order, str(block)], capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype="<f4").reshape(-1, 2).mean(1).astype(np.float64)
def band(sig, lo, hi):
    F = np.fft.rfft(sig); f = np.fft.rfftfreq(len(sig), 1 / SR); F[(f < lo) | (f > hi)] = 0; return np.fft.irfft(F, len(sig))
def envdb(sig, w_ms):
    w = int(SR * w_ms / 1000); return 20 * np.log10(np.sqrt(np.convolve(sig * sig, np.ones(w) / w, "same")) + 1e-9)
s16 = int(60.0 / BPM / 4 * SR)
def per_note(x):
    e = envdb(band(x, 40, 2000), 3); after_gap, in_run = [], []
    ons = sorted(set(n[0] for n in seq)); onset_set = set(ons)
    for on in ons:
        seg = e[on: on + int(0.95 * s16)]; jump = e[on + int(0.004 * SR)] - e[on - int(0.003 * SR)]
        t_full = np.argmax(seg >= seg.max() - 1) / SR * 1000
        (in_run if any(abs(on - s16 - o) < 30 for o in ons) else after_gap).append((jump, t_full, seg[int(0.004 * SR)] - seg.max()))
    return np.array(after_gap), np.array(in_run)
B2 = os.path.join(L, "bass_57238.xml"); B1 = os.path.join(L, "bass_39795.xml")
cases = [("Bass 2, конец→начало", B2, {}, "offfirst"), ("Bass 2, начало→конец", B2, {}, "onfirst"),
         ("Bass 1, конец→начало", B1, {}, "offfirst"), ("Bass 1, начало→конец", B1, {}, "onfirst")]
for nm, xml, over, order in cases:
    a, r = per_note(render(params(xml, **over), order))
    fmt = lambda z: "скачок в начале %+5.1f дБ, через 4 мс %5.1f дБ от максимума ноты, до полной %5.1f мс" % (np.median(z[:, 0]), np.median(z[:, 2]), np.median(z[:, 1]))
    print("%-22s | после паузы (%2d): %s\n%-22s | вплотную    (%2d): %s" % (nm, len(a), fmt(a), "", len(r), fmt(r)))
