#!/usr/bin/env python3

import json
import glob
import os


files=sorted(
    glob.glob("telemetry/history/*.json")
)


if len(files)<2:
    print("No previous metrics available")
    exit(0)


previous=files[-2]
current=files[-1]


with open(previous) as f:
    old=json.load(f)

with open(current) as f:
    new=json.load(f)


oldm=old["metrics"]
newm=new["metrics"]


checks={

    "latency_samples": "lower",
    "memory_kb": "lower",
    "cpu_percent": "lower",
    "build_size_kb": "lower"

}


results=[]


for key,direction in checks.items():

    a=oldm.get(key,0)
    b=newm.get(key,0)

    delta=b-a


    if delta==0:
        state="STABLE"

    elif direction=="lower" and delta<0:
        state="IMPROVED"

    elif direction=="lower" and delta>0:
        state="REGRESSION"

    else:
        state="CHANGED"


    results.append(
        {
            "metric":key,
            "previous":a,
            "current":b,
            "delta":delta,
            "state":state
        }
    )



with open("ivanna_oem_delta_report.html","w") as out:

    out.write("""
<html>
<head>
<title>IVANNA OEM Delta Report</title>
</head>
<body>

<h1>IVANNA OEM Regression Analysis</h1>

<table border="1">
<tr>
<th>Metric</th>
<th>Previous</th>
<th>Current</th>
<th>Delta</th>
<th>Status</th>
</tr>
""")

    for r in results:

        out.write(
f"""
<tr>
<td>{r['metric']}</td>
<td>{r['previous']}</td>
<td>{r['current']}</td>
<td>{r['delta']}</td>
<td>{r['state']}</td>
</tr>
"""
        )


    out.write("""
</table>
</body>
</html>
""")


print("OEM delta report generated")

