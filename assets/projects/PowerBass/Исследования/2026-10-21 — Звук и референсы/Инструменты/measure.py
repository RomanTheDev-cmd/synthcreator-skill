"""Read-only source measurements and isolated PowerBass 1.0.0 render diagnostics.

Uses NumPy and the project's WAV decoder. No installed plug-in or live source edits.
Outputs numerical measurements only. RMS matching is not perceptual loudness matching.
"""
from pathlib import Path
import csv
import hashlib
import json
import math
import os
import select
import subprocess
import sys
import time
import numpy as np

HERE = Path(__file__).resolve().parent.parent
PROJECT = HERE.parent.parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(PROJECT / "Tools"))
from wavread import read_wav

ROOT = Path.home() / "Library/Mobile Documents/com~apple~CloudDocs/definitionofhardtechno"
RUNNER = Path.home() / "Library/Caches/PowerBassResearch20261021/powerbass_tests_artefacts/Release/powerbass_tests"
BANDS = [(20, 60), (60, 120), (120, 300), (300, 1000), (1000, 3000), (3000, 8000), (8000, 20000)]
SIDES = [(20, 100), (100, 250), (250, 600), (600, 1500), (1500, 5000), (5000, 16000)]


def db(x):
    return float(20 * np.log10(max(float(x), 1e-15)))


def metrics(x, sr):
    x = x[:int(sr * 8)].astype(np.float64)
    mid, side = x.mean(axis=1), (x[:, 0] - x[:, 1]) / 2
    pk = np.max(np.abs(x))
    rms = np.sqrt(np.mean(x*x))
    hop = max(1, int(sr * .01))
    frame = x[:len(x)//hop*hop].reshape(-1, hop, 2)
    e = np.sqrt(np.mean(frame*frame, axis=(1, 2)))
    active = e > np.max(e) * 10**(-24/20)
    a = frame[active].reshape(-1, 2)
    ar = np.sqrt(np.mean(a*a)) if len(a) else rms
    m = np.fft.rfft(mid * np.hanning(len(mid)))
    s = np.fft.rfft(side * np.hanning(len(mid)))
    f = np.fft.rfftfreq(len(mid), 1/sr)
    power = np.abs(m)**2
    total = np.sum(power[(f >= 20) & (f < 20000)]) + 1e-30
    out = dict(peak_db=db(pk), rms_db=db(rms), crest_db=db(pk/rms),
               active_crest_db=db(pk/ar), active_fraction=float(np.mean(active)),
               duty12=float(np.mean(e > np.max(e)*10**(-12/20))),
               seconds=len(x)/sr, sr=int(sr))
    for lo, hi in BANDS:
        mask = (f >= lo) & (f < hi)
        out[f"e_{lo}"] = db(np.sqrt(np.sum(power[mask])/total))
    for lo, hi in SIDES:
        mask = (f >= lo) & (f < hi)
        out[f"side_{lo}"] = db(np.sqrt((np.sum(np.abs(s[mask])**2)+1e-30)/(np.sum(power[mask])+1e-30)))
    return out


def category(name):
    rules = [("Reese Basslines", "Reese"), ("HDIT3 Basslines", "HDIT3"),
             ("Rumble Bass Loops", "Rumble"), ("Signature Saw Basslines", "Signature Saw"),
             ("ITHTS Synth Basslines", "ITHTS Synth"), ("Wobble Bass", "Wobble"),
             ("Dark Acid Lines", "Dark Acid"), ("Running Basslines Clean", "Running Clean"),
             ("Running Basslines Dirty", "Running Dirty"), ("Bonus Basslines", "Bonus"),
             ("Bass Loops", "Other Bass")]
    return next((cat for key, cat in rules if key in name), "Other Bass")


def references():
    names = []
    for file in ["basslines.csv", "rumble_bass.csv"]:
        names += [r["file"] for r in csv.DictReader((PROJECT/"Референсы"/file).open())]
    rows = []
    for i, name in enumerate(dict.fromkeys(names)):
        path = ROOT/name
        x, sr = read_wav(path)
        rows.append({"file": name, "category": category(name),
                     "pcm_first8s_sha256": hashlib.sha256(x[:int(sr*8)].tobytes()).hexdigest(),
                     **metrics(x, sr)})
        if (i+1)%100 == 0: print("references", i+1, flush=True)
    write_csv("references.csv", rows)
    return rows


def write_csv(name, rows):
    with (HERE/"Измерения"/name).open("w", newline="") as f:
        w = csv.DictWriter(f, list(rows[0]))
        w.writeheader(); w.writerows(rows)


class Renderer:
    def __enter__(self):
        self.p = subprocess.Popen([str(RUNNER), "--serve"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        return self

    def __exit__(self, *args):
        self.p.stdin.close()
        self.p.wait(timeout=10)
        self.p.stdout.close()

    def render(self, params, root=38, pattern=0, length=3):
        line = " ".join(f"{k}={v:.9g}" for k,v in params.items())
        line += f" _root={root} _bpm=150 _len={length} _pattern={pattern}\n"
        self.p.stdin.write(line.encode()); self.p.stdin.flush()
        size = int(length*48000)*2*4
        parts, left = [], size
        deadline = time.monotonic()+30
        while left:
            if not select.select([self.p.stdout], [], [], max(0, deadline-time.monotonic()))[0]:
                raise TimeoutError("isolated renderer stalled")
            part = os.read(self.p.stdout.fileno(), left)
            if not part: raise RuntimeError("isolated renderer exited before returning audio")
            parts.append(part); left -= len(part)
        return np.frombuffer(b"".join(parts), dtype="<f4").reshape(-1, 2).astype(np.float64)


def diagnostic():
    lines = subprocess.check_output([str(RUNNER), "--dump-presets"], text=True).splitlines()
    presets = []
    for line in lines:
        name, cat, text = line.split("|")
        presets.append((name, cat, {k:float(v) for k,v in (t.split("=") for t in text.split())}))
    rows, safety = [], []
    with Renderer() as r:
        for name, cat, params in presets:
            for pattern in [0,1]:
                a = r.render(params, pattern=pattern)
                # MASTER is after all timbral stages. Very low diagnostic gain keeps
                # the final safety curve inactive; undo the linear gain offline.
                low = r.render({**params, "master":-60}, pattern=pattern)
                raw = low * 10**((params["master"]+60)/20)
                assert np.max(np.abs(low)) < .9
                pk = np.max(np.abs(raw), axis=1)
                mapped = .9 + .099*np.tanh(np.maximum(0, pk-.9)/.099)
                gain = np.where(pk > .9, mapped/np.maximum(pk, 1e-30), 1)
                error = np.max(np.abs(a - raw*gain[:,None]))
                assert error < 2e-6, (name, error)
                safety.append(dict(name=name, category=cat, pattern=pattern, master=params["master"],
                                   presafety_peak_db=db(np.max(pk)), limited_percent=100*float(np.mean(pk>.9)),
                                   max_reduction_db=-db(np.min(gain)), reconstruction_error=float(error)))
                rows.append(dict(name=name, category=cat, pattern=pattern, **metrics(a,48000)))
        # Controlled drive sweeps with no active output safety, identical D2 patterns.
        sweep=[]
        common=dict(shape=2,sub=.5,oscDrive=.2,cutoff=8000,envAmount=0,distTone=1,distMix=1,
                    dark=0,bite=0,punch=0,attack=1,decay=300,sustain=1,release=30,width=0,master=-60)
        for kind in [0,1,2,3,4]:
            for drive in [0,.25,.5,.7,.72,.724,.75,1]:
                a=r.render({**common,"distType":kind,"distDrive":drive})
                sweep.append(dict(distType=kind,drive=drive,**metrics(a,48000)))
        write_csv("drive_sweep.csv",sweep)
    write_csv("powerbass_patterns.csv",rows)
    write_csv("output_safety.csv",safety)
    return rows,safety


def selfcheck():
    # Independent analytic controls: sinusoid crest=3.0103 dB; relative spectrum,
    # crest and activity invariant to gain; unequal L/R must appear in side.
    t=np.arange(48000)/48000
    a=np.repeat(np.sin(2*np.pi*80*t)[:,None],2,axis=1)
    m,n=metrics(a,48000),metrics(a*.1,48000)
    assert abs(m["crest_db"]-3.0103)<.001
    for key in ["crest_db","active_crest_db","active_fraction","duty12","e_60"]:
        assert abs(m[key]-n[key])<1e-8,key
    assert metrics(np.column_stack([a[:,0],-a[:,0]*.5]),48000)["side_20"]>0
    # Known failing normalization condition from the committed engine.
    g=8*np.pi/1.25
    sampled=np.max(np.abs(np.sin(1.25*g*np.array([.25,.5,.75,1]))))
    wrong=np.clip(.85/max(sampled,1e-6),.2,4)
    true=np.max(np.abs(np.sin(1.25*g*np.linspace(-1,1,100001))))
    assert wrong==4 and abs(true-1)<1e-8
    print("selfcheck: analytic sine, gain invariance, stereo, Fold defect reproduced",flush=True)


if __name__=="__main__":
    selfcheck()
    if "--selfcheck" in sys.argv: sys.exit(0)
    if "--reuse-references" in sys.argv:
        refs=list(csv.DictReader((HERE/"Измерения/references.csv").open()))
        for row in refs:
            for key in list(row):
                if key not in ("file", "category", "pcm_first8s_sha256"):
                    row[key]=float(row[key])
    else:
        refs=references()
    rows,safety=diagnostic()
    summary={}
    for key,items in [("references",refs),("powerbass",rows)]:
        summary[key]={}
        for cat in sorted({x["category"] for x in items}):
            a=[x for x in items if x["category"]==cat]
            keys=["crest_db","active_crest_db","duty12","e_20","e_60","e_120","e_300","e_1000","e_3000","side_1500"]
            summary[key][cat]={"n":len(a),**{k:[round(float(v),3) for v in np.percentile([x[k] for x in a],[10,50,90])] for k in keys}}
    summary["safety"]={"patterns":len(safety),"any_activation":sum(x["limited_percent"]>0 for x in safety),
                       "worst":sorted(safety,key=lambda x:x["max_reduction_db"],reverse=True)[:10]}
    summary["runner_sha256"]=hashlib.sha256(RUNNER.read_bytes()).hexdigest()
    (HERE/"Измерения/summary.json").write_text(json.dumps(summary,indent=2,ensure_ascii=False))
    print(json.dumps(summary,ensure_ascii=False,indent=2),flush=True)
