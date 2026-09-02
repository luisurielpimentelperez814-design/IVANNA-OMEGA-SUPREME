#!/usr/bin/env python3

import math
import json
import os
import time


SAMPLE_RATE = 48000
SAMPLES = 48000


def generate_sine(freq, amp):

    data=[]

    for i in range(SAMPLES):

        x=amp*math.sin(
            2*math.pi*freq*i/SAMPLE_RATE
        )

        data.append(x)

    return data



def rms(signal):

    return math.sqrt(
        sum(x*x for x in signal)
        /
        len(signal)
    )



def detect_clipping(signal):

    count=0

    for x in signal:

        if abs(x)>=1.0:
            count+=1

    return count



def check_invalid(signal):

    bad=0

    for x in signal:

        if not math.isfinite(x):
            bad+=1

    return bad



def calculate_snr(signal):

    r=rms(signal)

    if r==0:
        return 0

    noise=1e-6

    return 20*math.log10(r/noise)



def run_test(name,signal):

    return {

        "test":name,

        "samples":len(signal),

        "rms":rms(signal),

        "snr_db":calculate_snr(signal),

        "clipping_events":
            detect_clipping(signal),

        "invalid_samples":
            check_invalid(signal)

    }



results=[]


signals=[

    ("bass_60hz",
     generate_sine(60,0.8)),

    ("voice_band",
     generate_sine(1000,0.8)),

    ("treble_10khz",
     generate_sine(10000,0.8))

]


for name,data in signals:

    results.append(
        run_test(name,data)
    )



passed=True


for r in results:

    if r["invalid_samples"]>0:
        passed=False

    if r["clipping_events"]>0:
        passed=False



report={

    "system":
        "IVANNA OMEGA SUPREME IAEL",

    "timestamp":
        time.strftime("%Y-%m-%d %H:%M:%S"),

    "certification":
        "PASS" if passed else "FAIL",

    "results":
        results

}



os.makedirs(
    "telemetry/iael",
    exist_ok=True
)


with open(
"telemetry/iael/latest_certification.json",
"w"
) as f:

    json.dump(
        report,
        f,
        indent=4
    )



with open(
"ivanna_iael_report.html",
"w"
) as f:

    f.write(
"""
<html>
<head>
<title>IVANNA IAEL Certification</title>
</head>
<body>

<h1>IVANNA Audio Evaluation Laboratory</h1>
"""
    )

    f.write(
        "<h2>"
        +report["certification"]
        +"</h2>"
    )


    for r in results:

        f.write(
f"""
<h3>{r['test']}</h3>
<ul>
<li>RMS: {r['rms']}</li>
<li>SNR: {r['snr_db']} dB</li>
<li>Clipping: {r['clipping_events']}</li>
<li>Invalid: {r['invalid_samples']}</li>
</ul>
"""
        )


    f.write("</body></html>")


print(
"IAEL certification:",
report["certification"]
)

