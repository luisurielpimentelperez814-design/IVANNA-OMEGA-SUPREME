#!/usr/bin/env python3
"""
export_dataset.py — Fase II/III: full export + reproducible analysis of the
Zenodo 10.5281/zenodo.20567124 REW dataset (kuulavaimennintesti202050914_nimetty.mdat).

Read-only on the .mdat. Produces, under --outdir:
  measurements/<slug>/meta.json                     (all decoded scalars + availability map)
  measurements/<slug>/frequency_response.csv        (sweeps: freq, spl, phase, unwrapped phase, group delay)
  measurements/<slug>/impulse_response.npy          (sweeps: float32 IR, NOT committed to git)
  measurements/<slug>/harmonics_sweep.csv           (sweeps: 1/96-oct harmonic curves from distortionData)
  measurements/<slug>/stepped_sine_thd.csv          (stepped-sine: per-point THD/THD+N/harmonics)
  analysis_summary.json                             (Fase III quantitative results)
Nothing is inferred: fields absent from the .mdat are reported as ABSENT.
"""
import json
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mdat_parser import Reader  # noqa: E402

BOXED = ("java.lang.Double", "java.lang.Integer", "java.lang.Boolean", "java.lang.Float", "java.lang.Long")


def unbox(v):
    if isinstance(v, dict) and "@class" in v and v["@class"] in BOXED and "value" in v:
        return v["value"]
    return v


def slug(name):
    return (name.replace(" ", "_").replace(",", ""))[:80]


def export_measurement(m, outdir):
    name = m.get("shortDesc") or "unnamed"
    s = slug(name)
    d = os.path.join(outdir, "measurements", s)
    os.makedirs(d, exist_ok=True)

    sr = m.get("sampleRate")
    n = m.get("dataLength")
    meta = {
        "name": name,
        "sourceType": m.get("sourceType"),
        "sampleRate": sr,
        "dataLength": n,
        "startFreq": m.get("startFreq"),
        "endFreq": m.get("endFreq"),
        "validStartFreq": m.get("validStartFreq"),
        "validEndFreq": m.get("validEndFreq"),
        "freqStep": m.get("freqStep"),
        "isLogSpaced": m.get("isLogSpaced"),
        "ppo": m.get("ppo"),
        "numSweeps": m.get("numSweeps"),
        "sourceFileDateMillis": m.get("sourceFileDate"),
        "sigGenLevel": m.get("sigGenLevelSt"),
        "sourceFileFormat": (m.get("sourceFileFormat") or "").strip(),
        "sourceFileName": m.get("sourceFileName"),
        "outputName": m.get("outputName"),
        "measNotes": (m.get("measNotes") or "").strip(),
        "rewVersion": f"{unbox(m.get('rewVersion'))}.{unbox(m.get('rewSubVersion'))}",
        "jre": m.get("jre"),
        "os": f"{m.get('osName')} {m.get('osVersion')}",
        "timingReferenceMode": m.get("timingReferenceMode"),
        "room": {"width": m.get("roomWidth"), "length": m.get("roomLength"), "height": m.get("roomHeight")},
        "available": {},
    }

    # --- frequency-domain curves (sweeps) ---
    spl = m.get("splValues")
    phase = m.get("phaseValues")
    unw = m.get("unwPhaseValues")
    gd = m.get("gdValues")
    ir_data = m.get("irData") or {}
    irsd = ir_data.get("ir") or {}
    ir = irsd.get("data")
    irt = unbox(irsd.get("T"))

    meta["available"]["spl"] = spl is not None
    meta["available"]["phase"] = phase is not None
    meta["available"]["unwrappedPhase"] = unw is not None
    meta["available"]["groupDelay"] = gd is not None
    meta["available"]["impulseResponse"] = ir is not None
    meta["available"]["steppedSineResults"] = m.get("steppedSineResults") is not None

    if spl is not None:
        freqs = (np.arange(len(spl), dtype=np.float64) * float(m.get("freqStep"))
                 + float(m.get("startFreq")))
        rows = np.column_stack([freqs, spl,
                                phase if phase is not None else [np.nan] * len(spl),
                                unw if unw is not None else [np.nan] * len(spl),
                                [g * 1000.0 if g is not None else np.nan for g in gd] if gd is not None else [np.nan] * len(spl)])
        np.savetxt(os.path.join(d, "frequency_response.csv"), rows, delimiter=",",
                   header="freq_hz,spl_db,phase_deg,unwrapped_phase_deg,group_delay_ms",
                   comments="", fmt="%.6f")
        arr = np.array(spl, dtype=np.float64)
        meta["nonfinite_spl_samples"] = int(np.sum(~np.isfinite(arr)))

    if ir is not None:
        a = np.asarray(ir, dtype=np.float64)
        np.save(os.path.join(d, "impulse_response.npy"), a.astype(np.float32))
        peak = int(np.argmax(np.abs(a)))
        meta["ir"] = {
            "n": len(a),
            "samplePeriod_s": irt,
            "irSampleRate_hz": (1.0 / irt) if irt else None,
            "startTime_s": unbox(irsd.get("startTime")),
            "absMaxVal": unbox(irsd.get("absMaxVal")),
            "absMaxIndex": unbox(irsd.get("absMaxIndex")),
            "peakIndex": peak,
            "peakTime_s": (unbox(irsd.get("startTime")) or 0.0) + peak * (irt or 0.0),
            "origPeakTime_s": unbox(ir_data.get("origPeakTime")),
            "splOffset_dB": unbox(ir_data.get("splOffset")),
            "nonfinite_samples": int(np.sum(~np.isfinite(a))),
            "window": {"preType": unbox((ir_data.get("windows") or {}).get("preType")),
                       "postType": unbox((ir_data.get("windows") or {}).get("postType")),
                       "preImpulseDurn_s": unbox((ir_data.get("windows") or {}).get("preImpulseDurn")),
                       "postImpulseDurn_s": unbox((ir_data.get("windows") or {}).get("postImpulseDurn"))},
        }

    # --- distortionData ---
    dd = m.get("distortionData") or {}
    if dd:
        meta["distortion"] = {
            "isFromIR": dd.get("isFromIR"),
            "isIMD": dd.get("isIMD"),
            "isMT": dd.get("isMT"),
            "sampleRate": dd.get("sampleRate"),
            "dataLength": dd.get("dataLength"),
            "startFreq": dd.get("startFreq"),
            "endFreq": dd.get("endFreq"),
            "ppo": dd.get("ppo"),
            "splOffset_dB": unbox(dd.get("splOffset")),
            "available": {
                "freqs": dd.get("freqs") is not None,
                "harmonics": dd.get("harmData") is not None,
                "noiseFloorSPL": dd.get("noiseFloorSPL") is not None,
                "thdPlusN": dd.get("thdPlusN") is not None,
                "levels": dd.get("levels") is not None,
                "imd": bool(dd.get("isIMD")),
                "spectrum": dd.get("spectrum") is not None,
            },
        }
        freqs = dd.get("freqs")
        harm = dd.get("harmData") or []
        if freqs and harm:
            cols = [np.asarray(freqs, dtype=np.float64)]
            header = ["freq_hz"]
            for hi, hvals in enumerate(harm, start=1):
                cols.append(np.asarray(hvals, dtype=np.float64))
                header.append(f"harmonic_{hi}_db")
            nf = dd.get("noiseFloorSPL")
            if nf:
                cols.append(np.asarray(nf, dtype=np.float64))
                header.append("noise_floor_spl_db")
            rows = np.column_stack(cols)
            np.savetxt(os.path.join(d, "harmonics_sweep.csv"), rows, delimiter=",",
                       header=",".join(header), comments="", fmt="%.4f")

    ssr = m.get("steppedSineResults")
    if ssr:
        # dBHarmonicData length varies per point (harmonics above Nyquist are
        # dropped by REW): pad rows to the max observed count with NaN.
        rows = []
        nh = max(len(p.get("dBHarmonicData") or []) for p in ssr)
        header = ["freq_hz", "gen_level_dbfs", "input_rms_db",
                  "thd_db", "thd_pct", "thdn_db", "thdn_pct",
                  "n_plus_d_pct", "db_n_and_nhd"] + [f"harmonic_{k}_db" for k in range(1, nh + 1)]
        for p in ssr:
            hb = list(p.get("dBHarmonicData") or [])
            hb += [float("nan")] * (nh - len(hb))
            rows.append([p.get("freq"), p.get("generatorLeveldBFS"), p.get("inputRMSdB"),
                         p.get("dBThd"), p.get("thd"), p.get("dBThdPlusN"), p.get("thdPlusN"),
                         p.get("nPlusD"), p.get("dBNandNHD")] + hb)
        np.savetxt(os.path.join(d, "stepped_sine_thd.csv"), np.array(rows, dtype=np.float64),
                   delimiter=",", header=",".join(header), comments="", fmt="%.6f")
        meta["stepped_sine"] = {"points": len(ssr), "max_harmonics_recorded": nh,
                                "harmonics_vary_by_point": True}

    with open(os.path.join(d, "meta.json"), "w") as f:
        json.dump(meta, f, indent=1)
    return meta


def analyze(meas, outdir):
    """Fase III quantitative analysis. All numbers come from decoded data only."""
    res = {"sweeps": {}, "stepped": {}, "comparisons": {}}
    by_name = {m.get("shortDesc"): m for m in meas}
    band = (200.0, 20000.0)

    def grid(m):
        spl = m["splValues"]
        return np.arange(len(spl)) * float(m["freqStep"]) + float(m["startFreq"])

    def spl_arr(m):
        return np.asarray(m["splValues"], dtype=np.float64)

    sweeps = [m for m in meas if m.get("splValues")]
    for m in sweeps:
        f = grid(m)
        s = spl_arr(m)
        bmask = (f >= band[0]) & (f <= band[1])
        ir = ((m.get("irData") or {}).get("ir")) or {}
        data = np.asarray(ir.get("data") or [], dtype=np.float64)
        res["sweeps"][m["shortDesc"]] = {
            "spl_mean_200hz_20khz_db": float(np.mean(s[bmask])),
            "spl_at_1khz_db": float(np.mean(s[(f >= 950) & (f <= 1050)])),
            "spl_max_db": float(np.max(s)),
            "ir_abs_max": float(np.max(np.abs(data))) if data.size else None,
            "ir_peak_index": int(np.argmax(np.abs(data))) if data.size else None,
        }

    # repeatability: same-condition pairs
    def delta_rms(a, b):
        fa, fb = grid(a), grid(b)
        assert np.allclose(fa, fb)
        d = spl_arr(a) - spl_arr(b)
        m = (fa >= band[0]) & (fa <= band[1])
        return {"mean_delta_db": float(np.mean(d[m])), "rms_delta_db": float(np.sqrt(np.mean(d[m] ** 2))),
                "max_abs_delta_db": float(np.max(np.abs(d[m])))}

    pairs = [("digi_vaimennin_impulssi", "digi_vaimennin_impulssi_2"),
             ("digi_ei_vaimenninta_impulssi", "digi_ei_vaimenninta_impulssi_2"),
             ("kaiuttimet_ei_vaiment_impulssi", "kaiuttimet_ei_vaiment_impulssi2")]
    for a, b in pairs:
        if a in by_name and b in by_name:
            res["comparisons"][f"repeatability::{a} vs {b}"] = delta_rms(by_name[a], by_name[b])

    # condition comparisons (microphone-domain SPL differences)
    conds = [("A_damped_vs_dac_nodamper", "digi_vaimennin_impulssi", "digi_ei_vaimenninta_impulssi"),
             ("B_damped_vs_speaker_nodamper", "digi_vaimennin_impulssi", "kaiuttimet_ei_vaiment_impulssi")]
    for label, a, b in conds:
        if a in by_name and b in by_name:
            res["comparisons"][label] = delta_rms(by_name[a], by_name[b])

    # stepped-sine THD summary + harmonic structure + computed THD
    stepped = [m for m in meas if m.get("steppedSineResults")]
    for m in stepped:
        pts = m["steppedSineResults"]
        freqs = [p["freq"] for p in pts]
        nh = max(len(p.get("dBHarmonicData") or []) for p in pts)

        def pad(p):
            h = list(p.get("dBHarmonicData") or [])
            return h + [float("nan")] * (nh - len(h))

        allh = np.array([pad(p) for p in pts], dtype=np.float64)
        h1 = allh[:, 0]
        hrest = allh[:, 1:]
        summary = {"freq_points": len(pts),
                   "max_harmonics_recorded": int(nh),
                   "generator_level_dbfs": sorted({p.get("generatorLeveldBFS") for p in pts}),
                   "imd_measured": any((p.get("imddB") or 0.0) != 0.0 for p in pts)}
        # computed THD (%) from harmonic magnitudes (derived metric); NaN-aware
        with np.errstate(invalid="ignore"):
            harm_pow = np.nansum(10 ** (hrest / 10.0), axis=1)
            thd_lin = np.sqrt(harm_pow) / 10 ** (h1 / 10.0)
            summary["computed_thd_pct_min"] = float(np.nanmin(thd_lin) * 100)
            summary["computed_thd_pct_max"] = float(np.nanmax(thd_lin) * 100)
            fq = np.asarray(freqs, dtype=np.float64)
            mask = fq < 1000.0
            mean_harm_power = np.nanmean(10.0 ** (hrest[mask] / 10.0), axis=0)
        summary["dominant_harmonic_below_1khz"] = int(np.nanargmax(np.nan_to_num(mean_harm_power)) + 2)
        res["stepped"][m["shortDesc"]] = summary

    # sweep-derived harmonic curves: level-vs-distortion check (single level => not possible)
    res["level_series_available"] = False
    res["notes"] = [
        "All sweep measurements share one excitation level (-12 dBFS); stepped-sine at -20 dBFS only.",
        "No raw excitation or output time series are embedded (MeasData.ir=null, audioDataLength=0).",
        "No IMD/multitone data (distortionData.isIMD=false, imddB placeholders 0.0).",
    ]
    return res


def main():
    mdat, outdir = sys.argv[1], sys.argv[2]
    data = open(mdat, "rb").read()
    r = Reader(data)
    items = r.read_stream()
    meas = [it for it in items
            if hasattr(it, "cls") and it.cls.name == "roomeqwizard.MeasData"]

    # convert JavaObj -> plain dict tree once (reuse mdat_parser.to_jsonable)
    from mdat_parser import to_jsonable
    meas = [to_jsonable(m) for m in meas]

    os.makedirs(outdir, exist_ok=True)
    metas = [export_measurement(m, outdir) for m in meas]
    summary = analyze(meas, outdir)
    with open(os.path.join(outdir, "analysis_summary.json"), "w") as f:
        json.dump(summary, f, indent=1)
    with open(os.path.join(outdir, "all_metadata.json"), "w") as f:
        json.dump(metas, f, indent=1)
    print(f"exported {len(metas)} measurements to {outdir}")
    for m in metas:
        print(f"  {m['name']}: sr={m['sampleRate']} n={m['dataLength']} ir={m['available']['impulseResponse']}")


if __name__ == "__main__":
    main()
