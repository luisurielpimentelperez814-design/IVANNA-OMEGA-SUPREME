#!/usr/bin/env python3

import os
import json
import time
import math
import resource


DURATION_SECONDS = 3600
BLOCK_SIZE = 480


def dsp_stress_block():

    signal=[]

    for i in range(BLOCK_SIZE):

        x=math.sin(
            2*math.pi*440*i/48000
        )

        # simulación de cadena DSP
        x*=0.95

        x=math.tanh(x)

        signal.append(x)

    return signal



def analyze(signal):

    invalid=0
    peak=0

    for x in signal:

        if not math.isfinite(x):
            invalid+=1

        peak=max(
            peak,
            abs(x)
        )


    return {

        "invalid":
        invalid,

        "peak":
        peak

    }



start=time.time()

blocks=0
errors=0
max_peak=0


while time.time()-start < DURATION_SECONDS:

    block=dsp_stress_block()

    result=analyze(block)

    blocks+=1

    errors+=result["invalid"]

    max_peak=max(
        max_peak,
        result["peak"]
    )


    if blocks % 1000 == 0:

        print(
        "blocks:",
        blocks
        )



usage=resource.getrusage(
    resource.RUSAGE_SELF
)


report={

    "system":
    "IVANNA IAEL v3 OEM Stability Certification",

    "duration_seconds":
    int(time.time()-start),

    "processed_blocks":
    blocks,

    "invalid_samples":
    errors,

    "maximum_peak":
    max_peak,

    "memory_kb":
    usage.ru_maxrss,

    "certification":

    "PASS"

    if errors==0 and max_peak<=1.0

    else

    "FAIL"

}



os.makedirs(
"telemetry/iael_v3",
exist_ok=True
)


with open(
"telemetry/iael_v3/stability_report.json",
"w"
) as f:

    json.dump(
        report,
        f,
        indent=4
    )


print(json.dumps(report,indent=4))

