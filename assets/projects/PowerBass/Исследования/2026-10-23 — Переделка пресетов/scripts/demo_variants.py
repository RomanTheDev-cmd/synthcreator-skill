"""Варианты Reese Core и Dark Pulse для выбора на слух: прежний, автоматический переподбор, ручные A и B.
Каждый файл — два такта партии Bass 2, пик −1 dBFS."""
import json, os, subprocess, sys, tempfile, wave
TOOLS, BIN, OLD, NEW, HAND, NOTES, OUT = sys.argv[1:8]
sys.path.insert(0, os.path.expanduser("~/Library/Caches/PowerKickTools/py")); sys.path.insert(0, TOOLS)
import numpy as np
import refit_bass as rb
SR, BPM = 48000, 133.0
old = {e["name"]: e for e in rb.library(OLD)}; new = {e["name"]: e for e in rb.library(NEW)}; hand = json.load(open(HAND))
reg = [o for o in json.load(open(NOTES)) if o["chunk"] == 260714][0]["notes"]
tick = 60.0 / BPM / 960.0 * SR; lead = int(0.05 * SR); bars2 = 2 * 4 * 960
seq = [(lead + int(round((p - 38400) * tick)), lead + int(round((p - 38400 + l) * tick)), pi, v / 127.0) for p, pi, v, l in reg if p - 38400 < bars2]
LEN = lead + int(round(bars2 * tick)) + int(0.25 * SR)
tmp = tempfile.mkdtemp(); nf = os.path.join(tmp, "n.txt"); open(nf, "w").write("\n".join("%d %d %d %.4f" % s for s in seq))
os.makedirs(OUT, exist_ok=True)
def render(p):
    pf = os.path.join(tmp, "p.txt"); open(pf, "w").write(" ".join("%s=%g" % kv for kv in p.items()))
    x = np.frombuffer(subprocess.run([BIN, "--notes", pf, nf, "offfirst", "512"], capture_output=True, check=True).stdout, dtype="<f4").reshape(-1, 2)[:LEN]
    return x / max(np.abs(x).max(), 1e-9) * 10 ** (-1 / 20)
def write(name, x):
    w = os.path.join(tmp, "x.wav")
    with wave.open(w, "wb") as f: f.setnchannels(2); f.setsampwidth(2); f.setframerate(SR); f.writeframes((x * 32767).astype("<i2").tobytes())
    subprocess.run(["afconvert", "-f", "m4af", "-d", "aac", "-b", "192000", w, os.path.join(OUT, name + ".m4a")], check=True)
for preset, letters in (("Reese Core", ("Reese Core A (жёсткий риз)", "Reese Core B (тёмный грязный)")),
                        ("Dark Pulse", ("Dark Pulse A (квадрат с щелчком фильтра)", "Dark Pulse B (пила-квадрат, клип)"))):
    write("%s — 1 прежний" % preset, render(old[preset]["params"]))
    write("%s — 2 автоматический" % preset, render(new[preset]["params"]))
    for i, h in enumerate(letters):
        write("%s — %d ручной %s" % (preset, 3 + i, h.split(" ", 2)[2]), render({**hand[h], "master": -12.0}))
print("готово:", sorted(os.listdir(OUT)))
