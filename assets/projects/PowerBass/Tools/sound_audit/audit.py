"""Independent sound audit of the INSTALLED PowerBass / PowerKick (the AU Logic loads), through sound_audit_host.
Unlike the unit tests (engine code inside the test binary), everything here goes through the plug-in binary as a
DAW drives it, and measures what the tests do not: aliasing, true peak, timing, block-size invariance, tuning,
CPU, long runs, MIDI edge cases, tails, mono compatibility, velocity.

  python3 audit.py <bass|kick> <check> [options]     checks: see CHECKS at the bottom
Env: PK_NUMPY_PATH (numpy), AUDIT_OUT (result folder), AUDIT_WORKERS (8).
"""
import json, os, re, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor
if os.environ.get("PK_NUMPY_PATH"):
    sys.path.insert(0, os.environ["PK_NUMPY_PATH"])
import numpy as np

HOST = os.path.expanduser("~/Library/Caches/SoundAuditMac/sound_audit_host_artefacts/Release/sound_audit_host")
ROOT = os.path.expanduser("~/Library/Mobile Documents/com~apple~CloudDocs/Главная рабочая папка ИИ/Проекты")
PLUG = {
    "bass": dict(name="PowerBass", au=os.path.expanduser("~/Library/Audio/Plug-Ins/Components/PowerBass.component"),
                 vst3=os.path.expanduser("~/Library/Audio/Plug-Ins/VST3/PowerBass.vst3"),
                 tests=os.path.expanduser("~/Library/Caches/PowerBassBuildUniversal/powerbass_tests_artefacts/Release/powerbass_tests")),
    "kick": dict(name="PowerKick", au=os.path.expanduser("~/Library/Audio/Plug-Ins/Components/PowerKick.component"),
                 vst3=os.path.expanduser("~/Library/Audio/Plug-Ins/VST3/PowerKick.vst3"),
                 tests=os.path.expanduser("~/Library/Caches/PowerKickBuildUniversal/powerkick_tests_artefacts/Release/powerkick_tests")),
}
KICK_NOTE = 36


def db(x):
    return 20 * np.log10(max(float(x), 1e-12))


class Host:
    """One hosted instance of the installed plug-in."""
    def __init__(self, which, fmt="au"):
        self.which = which
        self.p = subprocess.Popen([HOST, PLUG[which][fmt]], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL)
        head = self._line()
        if not head.startswith("ok"):
            raise RuntimeError(head)
        self.sr, self.latency = 48000, 0
        self.params = {}
        for l in self.cmd_lines("params"):
            i, name, norm, text, steps = l.split("|", 4)
            self.params[name] = dict(index=int(i), norm=float(norm), text=text, steps=int(steps))
        self.defaults = {k: v["norm"] for k, v in self.params.items()}

    def _line(self):
        return self.p.stdout.readline().decode("utf-8", "replace").rstrip("\n")

    def send(self, s):
        self.p.stdin.write((s + "\n").encode()); self.p.stdin.flush()

    def cmd(self, s):
        self.send(s)
        return self._line()

    def cmd_lines(self, s):
        self.send(s)
        out = []
        while True:
            l = self._line()
            if l == "end":
                return out
            out.append(l)

    def prepare(self, sr=48000, block=512):
        self.sr = sr
        self.latency = int(self.cmd("prepare %d %d" % (sr, block)).split()[1])
        return self.latency

    def set(self, name, text):
        return self.cmd("set %s|%s" % (name, text))

    def setn(self, name, norm):
        return self.cmd("setn %s|%.9g" % (name, norm))

    def render(self, frames, events=(), block=512, seed=1):
        """events: ('on', sample, note, vel) | ('off', sample, note) | ('bend', sample, v) | ('pn', sample, name, norm)
        | ('cc', sample, cc, value). Returns (frames x 2 array, max block us, mean block us)."""
        lines = ["render %d %d %d" % (frames, block, seed)]
        for e in events:
            if e[0] == "pn":
                lines.append("pn %d %d %.9g" % (e[1], self.params[e[2]]["index"], e[3]))
            else:
                lines.append(" ".join(str(int(v)) if not isinstance(v, str) else v for v in e))
        lines.append("end")
        self.p.stdin.write(("\n".join(lines) + "\n").encode()); self.p.stdin.flush()
        head = self._line().split()
        assert head[0] == "data", head
        n = int(head[1]) * 2 * 4
        buf = bytearray()
        while len(buf) < n:
            chunk = self.p.stdout.read(n - len(buf))
            if not chunk:
                raise RuntimeError("host died")
            buf += chunk
        return np.frombuffer(bytes(buf), dtype="<f4").reshape(-1, 2).astype(np.float64), float(head[2]), float(head[3])

    def close(self):
        try:
            self.send("quit"); self.p.wait(timeout=10)
        except Exception:
            self.p.kill()


# ---------------------------------------------------------------- presets
def param_names(which):
    s = open(os.path.join(ROOT, PLUG[which]["name"], "Source", "Params.cpp"), encoding="utf-8").read()
    return dict(re.findall(r'id::(\w+)(?:, 1 \})?, "([^"]+)"', s))


def factory_presets(which):
    out = subprocess.run([PLUG[which]["tests"], "--dump-presets"], capture_output=True, text=True).stdout.strip().split("\n")
    res = []
    for line in out:
        name, cat, vals = line.split("|")[:3]
        res.append((name, cat, {k: float(v) for k, v in (t.split("=") for t in vals.split())}))
    return res


def apply_preset(h, values, names):
    """Every parameter back to its default, then the preset's values (as the plug-in's own preset load does;
    OVERSAMPLE stays a per-instance setting)."""
    for name, norm in h.defaults.items():
        if name != "Oversampling" and abs(h.params[name]["norm"] - norm) > 0:
            h.setn(name, norm)
            h.params[name]["norm"] = norm
    for k, v in values.items():
        name = names[k]
        st = h.params[name]["steps"]
        if 1 < st <= 64:
            r = h.setn(name, round(v) / (st - 1))
        else:
            r = h.set(name, repr(float(v)))
        h.params[name]["norm"] = float(r.rsplit("|", 1)[1])
    h.render(int(0.3 * h.sr))  # let parameter smoothing settle, as on a DAW timeline before the first note


# ---------------------------------------------------------------- patterns (the same notes as the unit tests' render server)
def bass_pattern(root, pattern, bpm=150.0, length=3.0, sr=48000):
    step = 60.0 / bpm / 4.0
    notes = []
    if pattern == 2:
        notes.append((0.0, 0.6, root))
    k = 0
    while pattern != 2 and k * step < length - step:
        if not ((pattern == 0 and k % 4 == 0) or (pattern == 1 and k % 2 == 0)):
            gate = 0.75 if pattern == 0 else 2.05
            note = root + (3 if (k // 16) % 2 == 1 and k % 16 >= 12 else 0)
            notes.append((k * step, k * step + gate * step, note))
        k += 1
    ev = []
    for on, off, n in notes:
        ev += [("on", int(on * sr), n, 100), ("off", int(off * sr), n)]
    return ev


def kick_hits(times, vel=127, sr=48000):
    return [("on", int(t * sr), KICK_NOTE, vel) for t in times] + [("off", int(t * sr) + int(0.05 * sr), KICK_NOTE) for t in times]


# ---------------------------------------------------------------- measures
def true_peak(x, factor=4):
    """dBTP: peak after 4x band-limited (FFT) upsampling, per channel."""
    best = 0.0
    for ch in range(x.shape[1]):
        s = x[:, ch]
        n = len(s)
        S = np.fft.rfft(s)
        up = np.fft.irfft(S, n * factor) * factor
        best = max(best, float(np.max(np.abs(up))))
    return db(best)


def click_score(x, sr, win=0.005):
    """Largest second difference relative to the RMS of the second difference around it (10 ms, excluding the point);
    a discontinuity stands out, steady distortion does not. Returns (score, time)."""
    m = x.mean(axis=1)
    d = np.abs(np.diff(m, 2))
    if len(d) < 4 or np.max(d) < 1e-6:
        return 0.0, 0.0
    w = int(win * sr)
    c = np.cumsum(np.concatenate([[0.0], d * d]))
    idx = np.arange(len(d))
    lo = np.clip(idx - w, 0, len(d)); hi = np.clip(idx + w + 1, 0, len(d))
    local = np.sqrt(np.maximum((c[hi] - c[lo] - d * d) / np.maximum(hi - lo - 1, 1), 1e-20))
    score = d / (local + 1e-9 * np.max(d))
    i = int(np.argmax(score))
    return float(score[i]), i / sr


def mono_loss(x):
    e_lr = np.mean(x[:, 0] ** 2 + x[:, 1] ** 2) / 2
    e_m = np.mean(((x[:, 0] + x[:, 1]) / 2) ** 2)
    return 10 * np.log10(max(e_m, 1e-20) / max(e_lr, 1e-20))


def spectrum(x, sr, t0, t1):
    s = x[int(t0 * sr): int(t1 * sr)].mean(axis=1)
    n = 1 << int(np.ceil(np.log2(len(s))))
    X = np.abs(np.fft.rfft(s * np.hanning(len(s)), n)) ** 2
    return np.fft.rfftfreq(n, 1 / sr), X


def out_dir(which):
    d = os.environ.get("AUDIT_OUT") or os.path.join(ROOT, PLUG[which]["name"], "Исследования", "2026-10-24 — Независимая проверка звука")
    os.makedirs(d, exist_ok=True)
    return d


def parallel(which, items, fn, workers=None, prepare=(48000, 512)):
    """fn(host, item) for every item, on AUDIT_WORKERS hosts (one item at a time each)."""
    workers = workers or int(os.environ.get("AUDIT_WORKERS", 8))
    chunks = [items[i::workers] for i in range(workers)]
    def run(chunk):
        h = Host(which); h.prepare(*prepare)
        try:
            return [fn(h, it) for it in chunk]
        finally:
            h.close()
    res = []
    with ThreadPoolExecutor(workers) as ex:
        for part in ex.map(run, chunks):
            res += part
    return res


# ---------------------------------------------------------------- checks (one compact run)
def lib_item(h, item, which, names):
    name, cat, vals = item
    apply_preset(h, vals, names)
    sr = h.sr
    if which == "bass":
        ev = bass_pattern(36, 0, 150, 2.0)
        x, mx, mean = h.render(int(3.0 * sr), ev)
        body, tail = x[: int(2.0 * sr)], x[int(2.6 * sr):]
    else:
        ev = kick_hits([0.0, 0.4, 0.8, 1.2])
        x, mx, mean = h.render(int(3.2 * sr), ev)
        body, tail = x[: int(1.8 * sr)], x[int(2.9 * sr):]
    pre = h.render(int(0.05 * sr))[0]
    cs, ct = click_score(body, sr)
    return dict(name=name, cat=cat, finite=bool(np.all(np.isfinite(x))), peak=db(np.max(np.abs(body))), tp=true_peak(body),
                dc=float(np.max(np.abs(body.mean(axis=0)))), tail=db(np.max(np.abs(tail))), idle=db(np.max(np.abs(pre))),
                mono=mono_loss(body), click=cs, click_t=ct, cpu=mean / (512 / sr * 1e6))


def alias_item(h, item, which, names):
    """Aliasing: the same sound at 44.1 kHz and 192 kHz; energy below 20 kHz that exists only at 44.1 kHz."""
    name, cat, vals = item
    res = {}
    for sr in (44100, 192000):
        h.prepare(sr, 512); apply_preset(h, vals, names)
        if which == "bass":
            x = h.render(int(1.0 * sr), [("on", 0, 48, 100), ("off", int(0.9 * sr), 48)])[0]; f, P = spectrum(x, sr, 0.15, 0.85)
        else:
            x = h.render(int(0.6 * sr), kick_hits([0.0]))[0]; f, P = spectrum(x, sr, 0.0, 0.5)
        res[sr] = (f, P / np.sum(P))
    f44, P44 = res[44100]; f192, P192 = res[192000]
    m = f44 < 20000
    Q = np.interp(f44[m], f192, P192)
    excess = np.sum(np.maximum(P44[m] - 10 * Q, 0))  # bins 10 dB louder at 44.1 kHz than at 192 kHz
    h.prepare(48000, 512)
    return dict(name=name, alias=10 * np.log10(max(excess, 1e-15)))


def run_all(which):
    names = param_names(which)
    presets = factory_presets(which)
    o = out_dir(which); rep = []
    t0 = time.time()
    lib = parallel(which, presets, lambda h, it: lib_item(h, it, which, names))
    json.dump(lib, open(os.path.join(o, "library.json"), "w"), indent=0, ensure_ascii=False)
    bad = lambda k, f: [r for r in lib if f(r[k])]
    rep.append("Библиотека через установленный AU (%d пресетов, 48 кГц, блок 512):" % len(lib))
    rep.append("  не числа (NaN/inf): %d" % len([r for r in lib if not r["finite"]]))
    tp = sorted(lib, key=lambda r: -r["tp"])
    rep.append("  пик между отсчётами (true peak): выше 0 dBTP — %d, выше −0,5 — %d; худшие: %s" % (len(bad("tp", lambda v: v > 0)), len(bad("tp", lambda v: v > -0.5)), ", ".join("%s %.2f" % (r["name"], r["tp"]) for r in tp[:4])))
    rep.append("  постоянная составляющая > 0,01: %d (макс. %.4f)" % (len(bad("dc", lambda v: v > 0.01)), max(r["dc"] for r in lib)))
    rep.append("  хвост после нот (последние 0,3 с) громче −90 дБ: %d; худшие: %s" % (len(bad("tail", lambda v: v > -90)), ", ".join("%s %.0f" % (r["name"], r["tail"]) for r in sorted(lib, key=lambda r: -r["tail"])[:4])))
    rep.append("  звук в тишине до нот громче −120 дБ: %d" % len(bad("idle", lambda v: v > -120)))
    rep.append("  потеря в моно (L+R) хуже −3 дБ: %d; худшие: %s" % (len(bad("mono", lambda v: v < -3)), ", ".join("%s %.1f" % (r["name"], r["mono"]) for r in sorted(lib, key=lambda r: r["mono"])[:4])))
    cl = sorted(lib, key=lambda r: -r["click"])
    rep.append("  щелчки (выброс второй разности над окружением): медиана %.1f; выше 30 — %d; худшие: %s" % (np.median([r["click"] for r in lib]), len(bad("click", lambda v: v > 30)), ", ".join("%s %.0f @%.3f с" % (r["name"], r["click"], r["click_t"]) for r in cl[:5])))
    rep.append("  нагрузка CPU (доля реального времени): медиана %.1f %%, макс. %.1f %% (%s)" % (100 * np.median([r["cpu"] for r in lib]), 100 * max(r["cpu"] for r in lib), max(lib, key=lambda r: r["cpu"])["name"]))
    al = parallel(which, presets, lambda h, it: alias_item(h, it, which, names))
    json.dump(al, open(os.path.join(o, "aliasing.json"), "w"), indent=0, ensure_ascii=False)
    av = np.array([r["alias"] for r in al])
    rep.append("Алиасинг (44,1 против 192 кГц, %s): медиана %.0f дБ; хуже −40 дБ — %d, хуже −30 — %d; худшие: %s" % ("нота C3" if which == "bass" else "удар", np.median(av), int(np.sum(av > -40)), int(np.sum(av > -30)), ", ".join("%s %.0f" % (r["name"], r["alias"]) for r in sorted(al, key=lambda r: -r["alias"])[:5])))
    rep += special(which, names, presets)
    rep.append("время: %.0f с" % (time.time() - t0))
    open(os.path.join(o, "ОТЧЁТ.txt"), "w", encoding="utf-8").write("\n".join(rep) + "\n")
    print("\n".join(rep))


def special(which, names, presets):
    rep = []
    h = Host(which); lat = h.prepare(48000, 512)
    vals = presets[0][2]
    # Timing: hits at odd sample offsets with three block patterns; onset vs the note-on + reported latency.
    apply_preset(h, vals, names)
    note = 36
    ref = h.render(48000, [("on", 0, note, 110), ("off", 6000, note)])[0][:, 0]
    k0 = int(np.argmax(np.abs(ref) > 0.02 * np.max(np.abs(ref))))
    errs, diffs = [], []
    offs = [1003, 9001, 17777, 30011]
    seqs = {}
    for block, seed in ((512, 1), (64, 1), (0, 7), (0, 99)):
        ev = []
        for t in offs:
            ev += [("on", t, note, 110), ("off", t + 6000, note)]
        x = h.render(48000, ev, block=block, seed=seed)[0][:, 0]
        seqs[(block, seed)] = x
        for t in offs:
            seg = np.abs(x[t: t + 4000]); errs.append(int(np.argmax(seg > 0.02 * np.max(np.abs(ref)))) - k0)
    base = seqs[(512, 1)]
    for k, x in seqs.items():
        if k != (512, 1):
            diffs.append(db(np.sqrt(np.mean((x - base) ** 2))) - db(np.sqrt(np.mean(base ** 2))))
    rep.append("Точность во времени (нота в произвольном отсчёте, блоки 512/64/случайные): сдвиг начала %s отсчётов (0 = точно); задержка %d" % (sorted(set(errs)), lat))
    rep.append("Один и тот же звук при разных блоках: отличие от блока 512 — %s дБ (−200 = совпадает побитово)" % ", ".join("%.0f" % d for d in diffs))
    # MIDI edge cases: same note struck twice before its note-off; All Notes Off (CC 123); All Sound Off (CC 120).
    x = h.render(48000, [("on", 0, note, 100), ("on", 4800, note, 100), ("off", 7200, note), ("off", 14400, note)])[0]
    hold = db(np.sqrt(np.mean(x[8000:14000] ** 2))); after = db(np.max(np.abs(x[40000:])))
    rep.append("Та же нота дважды до отпускания: звук держится до второго note-off — уровень между ними %.0f дБ, через 0,5 с после — %.0f дБ" % (hold, after))
    for cc in (123, 120):
        x = h.render(48000, [("on", 0, note, 100), ("cc", 9600, cc, 0)])[0]
        rep.append("CC %d (%s) посреди ноты: через 0,6 с уровень %.0f дБ%s" % (cc, "All Notes Off" if cc == 123 else "All Sound Off", db(np.max(np.abs(x[38400:]))), "" if which == "bass" else " (кик короткий: показательно для баса)"))
    # Velocity response.
    pk = []
    for v in (1, 16, 32, 48, 64, 80, 96, 112, 127):
        x = h.render(24000, [("on", 0, note, v), ("off", 12000, note)])[0]; pk.append(db(np.max(np.abs(x))))
    mono = all(b >= a - 0.1 for a, b in zip(pk, pk[1:]))
    rep.append("Velocity 1…127: пик %s дБ — %s" % (" ".join("%.1f" % v for v in pk), "растёт монотонно" if mono else "НЕ монотонно"))
    # CPU at the heaviest setting: 8x oversampling, 64-sample blocks, a busy pattern.
    h.setn("Oversampling", 1.0); h.prepare(48000, 64)
    ev = bass_pattern(36, 0, 170, 8.0) if which == "bass" else kick_hits([i * 60 / 170 / 4 for i in range(110)])
    x, mx, mean = h.render(8 * 48000, ev, block=64)
    budget = 64 / 48000 * 1e6
    rep.append("CPU при 8x и блоке 64: в среднем %.0f %% реального времени, самый тяжёлый блок %.0f %%" % (100 * mean / budget, 100 * mx / budget))
    h.setn("Oversampling", 0.5); h.prepare(48000, 512)
    # Tuning and pitch bend (bass): a plain sine voice.
    if which == "bass":
        apply_preset(h, {}, names)
        for nm, t in (("Shape", "0"), ("FM Amount", "0"), ("Analog/FM Mix", "0"), ("Sub", "0"), ("Distortion Mix", "0"), ("Detune", "0"),
                      ("Punch", "0"), ("Cutoff", "18000"), ("Resonance", "0"), ("Env Amount", "0"), ("Dark", "0"), ("Bite", "0"), ("Width", "0"), ("Osc Drive", "0")):
            h.set(nm, t)
        h.render(14400)
        cents = []
        for n in (28, 33, 40, 45, 52, 57, 64):
            x = h.render(48000, [("on", 0, n, 100), ("off", 46000, n)])[0]
            f, P = spectrum(x, 48000, 0.2, 0.9)
            want = 440 * 2 ** ((n - 69) / 12); m = (f > want * 0.8) & (f < want * 1.25)
            i = np.argmax(P[m]); fi = f[m][i]
            # parabolic interpolation
            j = np.where(m)[0][i]; a, b, c = np.log(P[j - 1] + 1e-30), np.log(P[j] + 1e-30), np.log(P[j + 1] + 1e-30)
            fi = f[j] + 0.5 * (a - c) / (a - 2 * b + c) * (f[1] - f[0])
            cents.append(1200 * np.log2(fi / want))
        bend = []
        for v, st in ((16383, 2), (0, -2)):
            x = h.render(48000, [("bend", 0, v), ("on", 0, 45, 100), ("off", 46000, 45)])[0]
            f, P = spectrum(x, 48000, 0.2, 0.9); want = 110 * 2 ** (st / 12); m = (f > want * 0.8) & (f < want * 1.25)
            bend.append(1200 * np.log2(f[m][np.argmax(P[m])] / want))
        h.render(4800, [("bend", 0, 8192)])
        rep.append("Строй (синус, ноты E1…E4): отклонение %s центов; pitch bend ±2 полутона: ошибка %s центов" % (" ".join("%+.1f" % c for c in cents), " ".join("%+.0f" % c for c in bend)))
    # State restore while playing (a project reload mid-play).
    apply_preset(h, presets[1][2], names); h.cmd("save"); apply_preset(h, presets[0][2], names)
    ev = bass_pattern(36, 0, 150, 1.0) if which == "bass" else kick_hits([0.0, 0.2, 0.4])
    h.render(12000, ev[:6]); h.cmd("load")
    x = h.render(48000, ev)[0]
    rep.append("Загрузка состояния во время игры: числа %s, пик %.1f дБ, щелчок-оценка %.0f" % ("в порядке" if np.all(np.isfinite(x)) else "NaN!", db(np.max(np.abs(x))), click_score(x, 48000)[0]))
    h.close()
    return rep


if __name__ == "__main__":
    run_all(sys.argv[1])
