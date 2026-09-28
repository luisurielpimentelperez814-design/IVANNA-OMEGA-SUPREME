#!/usr/bin/env python3
# Compares two OEM metrics snapshots: perf deltas + IAEL v4 quality deltas.
# Exit 1 si el quality gate actual es FAIL (regresión certificada).
import json
import sys

current = sys.argv[1]
previous = sys.argv[2]

with open(current) as f:
    c = json.load(f)
with open(previous) as f:
    p = json.load(f)

cm, pm = c.get("metrics", {}), p.get("metrics", {})

print("IVANNA OEM PERFORMANCE DELTA")
for m in ["latency_samples", "build_size_kb", "cpu_percent", "memory_kb"]:
    old, new = pm.get(m, 0), cm.get(m, 0)
    print(f"{m}: {old} -> {new} delta={new - old}")
    if m == "latency_samples" and new - old > 0:
        print("WARNING latency regression")
    if m == "memory_kb" and new - old > 10240:
        print("WARNING memory growth")

print("\nIVANNA IAEL v4 QUALITY DELTA")
found = False
for m in ["thd_n_db_997", "snr_db_997", "imd_db", "flatness_pp_db", "crest_pink"]:
    old, new = pm.get(m), cm.get(m)
    if old is None or new is None:
        continue
    found = True
    print(f"{m}: {old} -> {new} delta={new - old}")
if not found:
    print("(sin métricas IAEL v4 en los snapshots — corrupto o de antes del lab v4)")

ocert = pm.get("iael_certification", "?")
ncert = cm.get("iael_certification", "?")
print(f"iael_certification: {ocert} -> {ncert}")
if ocert == "PASS" and ncert != "PASS":
    print("WARNING quality regression: certificación IAEL perdió PASS")
if cm.get("quality_gate") == "FAIL":
    print("ERROR quality gate FAIL")
    sys.exit(1)
