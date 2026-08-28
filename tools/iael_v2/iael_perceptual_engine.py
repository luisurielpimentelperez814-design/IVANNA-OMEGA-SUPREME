#!/usr/bin/env python3

import json
import math
import os
import time


SAMPLE_RATE = 48000


def rms(x):

    if not x:
        return 0

    return math.sqrt(
        sum(v*v for v in x)/len(x)
    )


def peak(x):

    return max(
        abs(v) for v in x
    )


def energy(x):

    return sum(
        v*v for v in x
    )


def phase_stability(left,right):

    error=0

    for a,b in zip(left,right):

        error += abs(a-b)

    return 1.0/(1.0+error)



def spectral_balance(signal):

    low=0
    mid=0
    high=0


    for i,x in enumerate(signal):

        if i < len(signal)//3:
            low += abs(x)

        elif i < (len(signal)*2)//3:
            mid += abs(x)

        else:
            high += abs(x)


    total=low+mid+high+1e-9


    return {

        "low":low/total,
        "mid":mid/total,
        "high":high/total

    }



def compare_versions(reference,current):


    er=(

        abs(
            energy(current)
            -
            energy(reference)
        )
        /
        (energy(reference)+1e-9)

    )


    score=100


    score-=er*100


    if peak(current)>1:
        score-=20


    score=max(
        0,
        min(100,score)
    )


    return score



def generate_reference():

    data=[]

    for i in range(48000):

        data.append(
            0.5*
            math.sin(
            2*math.pi*440*i/SAMPLE_RATE
            )
        )

    return data



reference=generate_reference()


# Aquí se conectará la salida real del DSP
processed=reference[:]


quality=compare_versions(
    reference,
    processed
)


report={

    "system":
    "IVANNA IAEL v2",

    "timestamp":
    time.strftime(
    "%Y-%m-%d %H:%M:%S"
    ),

    "quality_score":
    quality,

    "metrics":{

        "rms":
        rms(processed),

        "peak":
        peak(processed),

        "phase":
        phase_stability(
            reference,
            processed
        ),

        "spectral":
        spectral_balance(
            processed
        )

    }

}


os.makedirs(
"telemetry/iael_v2",
exist_ok=True
)


with open(
"telemetry/iael_v2/latest.json",
"w"
) as f:

    json.dump(
        report,
        f,
        indent=4
    )


print(
"IAEL v2 QUALITY SCORE:",
quality
)

