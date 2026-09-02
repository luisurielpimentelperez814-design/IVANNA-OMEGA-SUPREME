#!/usr/bin/env python3

import json
import glob
import sys


files=sorted(
    glob.glob("telemetry/history/*.json")
)


if len(files)<2:
    print("No baseline available. PASS")
    sys.exit(0)


with open(files[-2]) as f:
    old=json.load(f)

with open(files[-1]) as f:
    new=json.load(f)


oldm=old.get("metrics",{})
newm=new.get("metrics",{})


limits={

    "latency_samples":0.10,
    "memory_kb":0.10,
    "cpu_percent":0.10,
    "build_size_kb":0.15

}


failed=False


print("IVANNA OEM QUALITY GATE")
print("=======================")


for metric,limit in limits.items():

    before=oldm.get(metric,0)
    after=newm.get(metric,0)


    if before==0:
        continue


    increase=(after-before)/before


    status="PASS"


    if increase>limit:
        status="FAIL"
        failed=True


    print(
        f"{metric}: "
        f"{before} -> {after} "
        f"change={increase*100:.2f}% "
        f"{status}"
    )


print("=======================")


if failed:

    print(
    "QUALITY GATE FAILED: "
    "performance regression detected"
    )

    sys.exit(1)


print(
"QUALITY GATE PASSED: "
"no unacceptable regression"
)

