"""Builds the 300-preset factory library of PowerBass (Source/FactoryPresets.inc).

  python3 build_library.py CURATED.json NAMES.json EXTRA.json DEEP_SUB_SOURCE.inc

Curated presets keep their names; extra presets get descriptive names from their sound and a
browser group (Reese / FM / Plucks / Sustained). Factory SPLIT stays >= 150 Hz (mono sub).
"""
import json, re, sys

INT = {"fmRatio", "filterMode", "distType", "filterPos", "glideMode", "oversample"}
ORDER = ["Dark Underground Bass", "Hard Techno Rumble Bass", "Schranz Distorted Bass", "Industrial FM Bass", "Metallic Bass",
         "Raw Analog Bass", "Deep Sub Bass", "Acid-Inspired Bass", "Rolling Techno Bass", "Destroyed Bass"]
GROUPS = ["Reese", "FM", "Plucks", "Sustained"]
NOUN = {"Dark Underground Bass": "Line", "Hard Techno Rumble Bass": "Rumble", "Schranz Distorted Bass": "Saw",
        "Industrial FM Bass": "Grind", "Metallic Bass": "Metal", "Raw Analog Bass": "Pulse", "Acid-Inspired Bass": "Acid",
        "Rolling Techno Bass": "Roller", "Destroyed Bass": "Wreck", "Deep Sub Bass": "Sub"}


def lit(k, v):
    if k in INT:
        return str(int(round(v)))
    t = "%.4g" % v
    if "." not in t and "e" not in t:
        t += ".0"
    return t + "f"


def group_of(p):
    if p.get("detune", 0) > 0.3:
        return "Reese"
    if p.get("fmAmount", 0) > 0.45 and p.get("oscMix", 0) > 0.35:
        return "FM"
    if p.get("sustain", 0.8) < 0.35 and p.get("decay", 300) < 400:
        return "Plucks"
    return "Sustained"


def base_name(p, cat):
    if p.get("dark", 0) > 0.5: adj = "Dark"
    elif p.get("bite", 0) > 0.5: adj = "Biting"
    else:
        adj = ["Warm", "Hard", "Folded", "Wrecked", "Crushed"][int(round(p.get("distType", 0)))]
        if adj == "Warm" and p.get("distDrive", 0.3) > 0.6: adj = "Heavy"
    g = group_of(p)
    noun = "Reese" if g == "Reese" else ("FM" if g == "FM" else NOUN[cat])
    return "%s %s" % (adj, noun)


def main(curated_json, names_json, extra_json, deep_inc):
    names = json.load(open(names_json))
    entries = []
    for m in json.load(open(curated_json)):
        key = m["medoid"].split("/")[-1]
        entries.append({"name": names[key], "cat": m["category"], "p": dict(m["params"]), "curated": True,
                        "comment": "fit %.1f to %s (cluster of %d)" % (m["distance"], key, m["cluster_size"])})
    used = {e["name"] for e in entries}
    counters = {}
    for m in json.load(open(extra_json)):
        p = dict(m["params"])
        b = base_name(p, m["category"])
        counters[b] = counters.get(b, 0) + 1
        n = "%s %02d" % (b, counters[b])
        while n in used:
            counters[b] += 1
            n = "%s %02d" % (b, counters[b])
        used.add(n)
        entries.append({"name": n, "cat": m["category"], "p": p, "curated": False,
                        "comment": "fit %.1f to %s" % (m["distance"], m["medoid"].split("/")[-1])})
    deep = re.findall(r'\{ "[^"]+", "Deep Sub Bass", \{.*?\} \},', open(deep_inc).read())
    out = ["// PowerBass factory presets (300). Unlisted parameters use defaults. Included by Presets.cpp.",
           "// All categories except Deep Sub are fitted to reference bass loops (Tools/fit_bass.py; numbers only,",
           "// no audio). Format: { name, category, { {id, value}... }, browser group }.", ""]
    for c in ORDER:
        es = [e for e in entries if e["cat"] == c]
        out.append("// --- %s (%d) %s" % (c, len(es) if c != "Deep Sub Bass" else len(deep), "-" * 40))
        if c == "Deep Sub Bass":
            out += [d.replace(" } },", " }, \"Sustained\" },") for d in deep]
        es.sort(key=lambda e: (GROUPS.index(group_of(e["p"])), not e["curated"], e["name"]))
        for e in es:
            p = e["p"]
            p["splitFreq"] = max(150.0, p.get("splitFreq", 300.0))
            kv = ",".join('{"%s",%s}' % (k, lit(k, p[k])) for k in sorted(p))
            out.append('{ "%s", "%s", { %s }, "%s" }, // %s' % (e["name"], c, kv, group_of(p), e["comment"]))
        out.append("")
    open("Source/FactoryPresets.inc", "w").write("\n".join(out))
    print(sum(1 for l in out if l.startswith('{ "')), "presets")


if __name__ == "__main__":
    main(*sys.argv[1:5])
