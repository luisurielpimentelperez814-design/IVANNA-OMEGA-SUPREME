#!/usr/bin/env python3
"""Lee /data/local/tmp/omega_effect_evidence (publicado por omega_effect) y decide, por
procesador, si esta APLICANDO de verdad entre dos lecturas separadas --interval segundos.
No usa indicadores de UI: solo contadores y energias medidos en el camino de audio.
Uso (root, en el telefono):  python3 read_processor_evidence.py [--interval 3] [--path ...]"""
import argparse, struct, sys, time

FMT = "<IIQ QQQQ QQQ dd dd ffff ffff II QII Q"
SIZE = struct.calcsize(FMT)
NAMES = ("magic version seq hrtfCpu hrtfDsp hrtfSafRefresh hrtfPose rirLoads rirRelinks rirActive "
         "hrtfIn hrtfOut rirIn rirOut hrtfAz hrtfBias hrtfCurv rirWet rirEarly rirDecor rirRt60 rirWetScale sr dataset controlGeneration hrtfEnabled adaptiveEnabled adaptiveApplyBlocks").split()

def read(path):
    for _ in range(50):
        with open(path, "rb") as f:
            raw = f.read(256)
        if len(raw) < SIZE: sys.exit("evidencia incompleta: el efecto no la ha publicado")
        d = dict(zip(NAMES, struct.unpack_from(FMT, raw)))
        if d["magic"] != 0x4F454556: sys.exit("magic invalido")
        if d["seq"] % 2 == 0: return d
        time.sleep(0.002)
    sys.exit("seqlock no estable")

def verdict(a, b):
    out = {}
    if b["hrtfCpu"] > a["hrtfCpu"]:
        dIn, dOut = b["hrtfIn"] - a["hrtfIn"], b["hrtfOut"] - a["hrtfOut"]
        out["HRTF"] = (("[dataset medido] " if b["dataset"] else "[SINTESIS analitica, sin HRIR medidas] ") + "APLICA (CPU), ganancia energia %.2f dB" % (10 * __import__('math').log10(max(dOut, 1e-12) / max(dIn, 1e-12)))
                       if dIn > 1e-9 else "CPU procesa pero la entrada es silencio: sin prueba")
    elif b["hrtfDsp"] > a["hrtfDsp"]:
        out["HRTF"] = "delegado al Hexagon: SIN medicion propia (no verificable desde aqui)"
    else:
        out["HRTF"] = "NO PROCESA en el intervalo"
    if b["rirActive"] > a["rirActive"]:
        dIn, dOut = b["rirIn"] - a["rirIn"], b["rirOut"] - a["rirOut"]
        out["RIR"] = ("APLICA, ganancia energia %.2f dB (wet=%.2f)" % (10 * __import__('math').log10(max(dOut, 1e-12) / max(dIn, 1e-12)), b["rirWet"])
                      if dIn > 1e-9 else "convoluciona pero la entrada es silencio: sin prueba")
    else:
        out["RIR"] = "NO CONVOLUCIONA (wet=%.2f, cargas=%d)" % (b["rirWet"], b["rirLoads"])
    out["ADAPTATIVO"] = (
        ("APLICA" if b["adaptiveEnabled"] and b["adaptiveApplyBlocks"] > a["adaptiveApplyBlocks"] else "SIN BLOQUES APLICADOS")
        + " | blocks=%d enabled=%d" % (b["adaptiveApplyBlocks"], b["adaptiveEnabled"])
    )
    out["RUTA B"] = "generation=%d HRTF=%d datasetMedido=%d CPU=%d DSP=%d" % (b["controlGeneration"], b["hrtfEnabled"], b["dataset"], b["hrtfCpu"], b["hrtfDsp"])
    saf = []
    if b["hrtfSafRefresh"] > a["hrtfSafRefresh"] or b["hrtfBias"] != a["hrtfBias"]: saf.append("HRTF re-sintetizado por SAF")
    if b["rirRelinks"] > a["rirRelinks"]: saf.append("RIR re-horneada por SAF")
    out["SAF"] = ("; ".join(saf) if saf else "sin cambios de q en el intervalo") + \
        " | bias=%.2f deg curv=%.3f rirDecor=%.2f rirEarly=%.2f rirRt60d=%.3f wetScale=%.2f" % (
            b["hrtfBias"], b["hrtfCurv"], b["rirDecor"], b["rirEarly"], b["rirRt60"], b["rirWetScale"])
    return out

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--path", default="/data/local/tmp/omega_effect_evidence")
    ap.add_argument("--interval", type=float, default=3.0)
    a = ap.parse_args()
    r0 = read(a.path); time.sleep(a.interval); r1 = read(a.path)
    for k, v in verdict(r0, r1).items(): print(f"{k:5s} {v}")
