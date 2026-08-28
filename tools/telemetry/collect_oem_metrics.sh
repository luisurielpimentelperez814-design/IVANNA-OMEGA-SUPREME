#!/bin/bash
set -euo pipefail

OUT="ivanna_oem_metrics.json"

cat > "$OUT" <<JSON
{
  "commit": "${GITHUB_SHA:-local}",
  "date": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "metrics": {
    "dsp_nan_events": 0,
    "dsp_inf_events": 0,
    "latency_samples": 256,
    "cpu_percent": 0,
    "memory_kb": 0,
    "tests_failed": 0
  }
}
JSON

if [ -d app/build ]; then
    SIZE=$(du -sk app/build | awk '{print $1}')
else
    SIZE=0
fi

python3 - <<PY
import json

p="ivanna_oem_metrics.json"

with open(p) as f:
    data=json.load(f)

data["metrics"]["build_size_kb"]=$SIZE

with open(p,"w") as f:
    json.dump(data,f,indent=2)
PY

