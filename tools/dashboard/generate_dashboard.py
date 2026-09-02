#!/usr/bin/env python3

import json
import glob
from datetime import datetime


FILES=sorted(
    glob.glob("telemetry/history/*.json")
)


rows=[]

for f in FILES:

    with open(f) as fd:
        data=json.load(fd)

    m=data.get("metrics",{})

    rows.append({
        "commit":data.get("commit","unknown")[:8],
        "date":data.get("date",""),
        "latency":m.get("latency_samples",0),
        "cpu":m.get("cpu_percent",0),
        "memory":m.get("memory_kb",0),
        "size":m.get("build_size_kb",0)
    })


with open("ivanna_oem_dashboard.html","w") as out:

    out.write("""
<!DOCTYPE html>
<html>
<head>
<title>IVANNA OEM Dashboard</title>
<style>
body {font-family:Arial;}
table {border-collapse:collapse;}
td,th {padding:8px;border:1px solid #888;}
</style>
</head>

<body>

<h1>IVANNA OMEGA SUPREME</h1>
<h2>OEM Performance Dashboard</h2>

<table>
<tr>
<th>Commit</th>
<th>Date</th>
<th>Latency</th>
<th>CPU</th>
<th>Memory</th>
<th>Build Size</th>
</tr>
""")

    for r in rows:

        out.write(
f"""
<tr>
<td>{r['commit']}</td>
<td>{r['date']}</td>
<td>{r['latency']}</td>
<td>{r['cpu']}</td>
<td>{r['memory']}</td>
<td>{r['size']}</td>
</tr>
"""
        )


    out.write("""
</table>

</body>
</html>
""")


print("Dashboard generated:")
print("ivanna_oem_dashboard.html")

