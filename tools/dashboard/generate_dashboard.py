#!/usr/bin/env python3
# IVANNA OEM Dashboard — consumo los resultados versionados del lab IAEL v4.
import glob
import json
import os
from datetime import datetime


def load_history():
    rows = []
    for f in sorted(glob.glob("telemetry/history/*.json")):
        try:
            with open(f) as fd:
                data = json.load(fd)
        except Exception:
            continue
        m = data.get("metrics", {})
        rows.append({
            "commit": str(data.get("commit", "unknown"))[:8],
            "date": data.get("date", ""),
            "latency": m.get("latency_samples", 0),
            "cpu": m.get("cpu_percent", 0),
            "memory": m.get("memory_kb", 0),
            "size": m.get("build_size_kb", 0),
        })
    return rows


def load_lab():
    p = "telemetry/iael_v4/latest.json"
    if not os.path.exists(p):
        return None
    with open(p) as f:
        lab = json.load(f)
    m = lab.get("metrics", {})
    return {
        "cert": lab.get("certification", "?"),
        "thd": m.get("thd_n_sine_997"),
        "snr": m.get("snr_sine_997"),
        "imd": m.get("imd_db"),
        "flat": m.get("flatness_pp_db"),
        "stereo": m.get("stereo", {}).get("pearson"),
        "clipping": m.get("clipping_events", 0),
        "invalid": m.get("invalid_samples", 0),
    }


def fmt(v, suffix=""):
    if v is None:
        return "—"
    if isinstance(v, float):
        return f"{v:.1f}{suffix}"
    return f"{v}{suffix}"


history = load_history()
lab = load_lab()
rows_html = ""
for r in history:
    rows_html += (
        f"<tr><td>{r['commit']}</td><td>{r['date']}</td>"
        f"<td>{r['latency']}</td><td>{r['cpu']}</td>"
        f"<td>{r['memory']}</td><td>{r['size']}</td></tr>\n"
    )

lab_html = "<tr><td colspan='6'>— sin lab IAEL v4 —</td></tr>"
if lab:
    lab_html = (
        f"<tr><td><b>{lab['cert']}</b></td><td>{fmt(lab['thd'], ' dB')}</td>"
        f"<td>{fmt(lab['snr'], ' dB')}</td><td>{fmt(lab['imd'], ' dB')}</td>"
        f"<td>{fmt(lab['flat'], ' dB')}</td>"
        f"<td>{fmt(lab['stereo'])} · clip {lab['clipping']} · NaN/Inf {lab['invalid']}</td></tr>\n"
    )

html = f"""<!DOCTYPE html>
<html>
<head>
<title>IVANNA OEM Dashboard</title>
<style>
body {{font-family:Arial;margin:24px;background:#0d1117;color:#e6edf3;}}
h1,h2 {{color:#23F09A;}}
table {{border-collapse:collapse;width:100%;}}
td,th {{padding:8px;border:1px solid #30363d;text-align:left;}}
th {{background:#161b22;color:#23F09A;}}
.ok {{color:#23F09A;font-weight:bold;}}
.bad {{color:#ff7b72;font-weight:bold;}}
</style>
</head>
<body>
<h1>⬡ IVANNA OMEGA SUPREME</h1>
<h2>OEM Performance Dashboard</h2>
<table>
<tr><th>Commit</th><th>Date</th><th>Latency</th><th>CPU</th><th>Memory</th><th>Build Size</th></tr>
{rows_html}
</table>
<h2>IAEL v4 — Certificación de Laboratorio</h2>
<table>
<tr><th>Cert</th><th>THD+N 997 Hz</th><th>SNR 997 Hz</th><th>IMD SMPTE</th><th>Planitud pp</th><th>Estéreo / validez</th></tr>
{lab_html}
</table>
<p>Generado: {datetime.utcnow().strftime('%Y-%m-%d %H:%M:%S')} UTC · lab: tools/iael_v4/iael_lab_engine.py</p>
</body>
</html>
"""
with open("ivanna_oem_dashboard.html", "w") as out:
    out.write(html)
print("Dashboard escrito: ivanna_oem_dashboard.html | cert lab:", lab["cert"] if lab else "N/A")
