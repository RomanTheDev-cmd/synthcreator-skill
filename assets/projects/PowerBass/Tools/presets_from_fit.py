"""Writes Source/FactoryPresets.inc from a fit_bass.py result plus the hand-made Deep Sub rows.

  python3 presets_from_fit.py FIT.json NAMES.json DEEP_SUB_SOURCE.inc
"""
import json, re, sys

INT = {"fmRatio", "filterMode", "distType", "filterPos"}
ORDER = ["Dark Underground Bass", "Hard Techno Rumble Bass", "Schranz Distorted Bass", "Industrial FM Bass", "Metallic Bass",
         "Raw Analog Bass", "Deep Sub Bass", "Acid-Inspired Bass", "Rolling Techno Bass", "Destroyed Bass"]


def lit(k, v):
    """A valid C++ literal: ints for choices, otherwise a float literal that always has a '.' or exponent."""
    if k in INT:
        return str(int(round(v)))
    t = "%.4g" % v
    if "." not in t and "e" not in t:
        t += ".0"
    return t + "f"


fits = json.load(open(sys.argv[1]))
names = json.load(open(sys.argv[2]))
deep = re.findall(r'\{ "[^"]+", "Deep Sub Bass", \{.*\} \},', open(sys.argv[3]).read())
out = ["// PowerBass factory presets: 10 categories x 5. Unlisted parameters use defaults.",
       "// Included by Presets.cpp. Keys are parameter IDs (see Params.h).",
       "// All categories except Deep Sub are fitted to typical reference bass loops (Tools/fit_bass.py:",
       "// k-medoids per category, synthesis parameters matched to the medoid; numbers only, no audio).", ""]
for c in ORDER:
    out.append("// --- %s %s" % (c, "-" * (70 - len(c))))
    if c == "Deep Sub Bass":
        out.append("// Hand-made: the reference folder has no pure sub-bass loops.")
        out += deep
    else:
        for m in [m for m in fits if m["category"] == c]:
            key = m["medoid"].split("/")[-1]
            p = dict(m["params"])
            # Club mono-compatibility (spec: the sub stays centred): factory SPLIT >= 150 Hz, so even a
            # D2 fundamental (73 Hz) is two LR4 slopes below the widened band.
            p["splitFreq"] = max(150.0, p.get("splitFreq", 300.0))
            kv = ",".join('{"%s",%s}' % (k, lit(k, p[k])) for k in sorted(p))
            out.append('{ "%s", "%s", { %s } }, // fit %.1f to %s (cluster of %d)' % (names[key], c, kv, m["distance"], key, m["cluster_size"]))
    out.append("")
open("Source/FactoryPresets.inc", "w").write("\n".join(out))
print(sum(1 for l in out if l.startswith('{ "')), "presets")
