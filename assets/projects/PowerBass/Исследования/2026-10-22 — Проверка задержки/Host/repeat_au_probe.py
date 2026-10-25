#!/usr/bin/env python3
"""Rebuild and repeat offline installed-AU probes; never opens an audio device."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import subprocess
import re

parser = argparse.ArgumentParser()
parser.add_argument("tag", help="New result-folder name, e.g. after-latency-fix")
args = parser.parse_args()
if not re.fullmatch(r"[A-Za-z0-9_-]+", args.tag):
    parser.error("Use letters, digits, dash or underscore")
root = Path(__file__).resolve().parent
out = root / args.tag
out.mkdir(exist_ok=False)

def manifest():
    result = []
    for name in ("PowerKick", "PowerBass"):
        for folder, suffix in (("Components", "component"), ("VST3", "vst3")):
            path = Path.home() / "Library/Audio/Plug-Ins" / folder / (name + "." + suffix) / "Contents/MacOS" / name
            result.append({"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    return result

before = manifest()
summary = {"utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
           "installed_before": before, "runs": []}
failures = 0
try:
    for source in ("au_probe", "au_parameters"):
        cmd = ["xcrun", "clang++", "-std=c++17", "-O2", str(root / (source + ".cpp")),
               "-framework", "AudioToolbox", "-framework", "AudioUnit", "-framework", "CoreFoundation",
               "-o", str(out / source)]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        (out / (source + "-build.log")).write_text(result.stdout + result.stderr)
        summary["runs"].append({"command": cmd, "exit_code": result.returncode})
        if result.returncode:
            raise RuntimeError(source + " did not compile")
    for index in (0, 1, 2):
        env = os.environ.copy()
        env.update(AU_PROBE_WARMUP="1", AU_PROBE_OS=str(index))
        result = subprocess.run([str(out / "au_probe")], env=env, capture_output=True, text=True, timeout=150)
        (out / ("os" + str(index) + ".jsonl")).write_text(result.stdout)
        (out / ("os" + str(index) + ".stderr.log")).write_text(result.stderr)
        summary["runs"].append({"oversampling_index": index, "warmup_seconds": 1, "exit_code": result.returncode})
        failures += result.returncode != 0
        for line in result.stdout.splitlines():
            if "latency_" in line or "failed_comparisons" in line:
                print(line, flush=True)
    result = subprocess.run([str(out / "au_parameters")], capture_output=True, text=True, timeout=60)
    (out / "parameters.jsonl").write_text(result.stdout)
    (out / "parameters.stderr.log").write_text(result.stderr)
    summary["runs"].append({"parameter_roundtrip_exit": result.returncode})
    failures += result.returncode != 0
finally:
    summary["installed_after"] = manifest()
    summary["installed_unchanged"] = before == summary["installed_after"]
    failures += not summary["installed_unchanged"]
    summary["failed_processes_or_manifest"] = failures
    (out / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2))
print("Result folder:", out)
print("Waveform comparisons pass does not independently validate reported PDC.")
raise SystemExit(1 if failures else 0)
