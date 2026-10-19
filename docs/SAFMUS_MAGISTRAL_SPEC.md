# SAFMUS MAGISTRAL — specification for the SAF evolution

Status: engineering specification and research plan. This document does not claim that the decision model is trained, integrated, or perceptually validated.

## Product definition

SAF is the existing spatial-audio framework and real-time rendering path. SAFMUS MAGISTRAL is its proposed supervisory layer: it observes validated route/context/evidence, chooses which existing SAF/IVANNA capability may be used, proposes bounded parameters, and verifies the result before activation. It must not duplicate the audio engine or create a second concurrent processing route.

Primary loop:

1. Observe route, format, processing state, calibration/measurement availability, user intent and safety constraints.
2. Build an evidence record with provenance and confidence.
3. Route only to compatible specialist capability.
4. Generate a bounded candidate, with an explanation and expected measurable effect.
5. Validate stability, headroom, latency budget and context compatibility.
6. Crossfade a validated candidate into the existing SAF path.
7. Monitor the result; rollback or abstain when evidence or constraints fail.

## Capability routing

| Capability | Required evidence | Permitted decision |
|---|---|---|
| Binaural/HRTF | Headphone or binaural output route; HRTF metadata and coordinate convention | Select compatible HRTF/HRIR and spatial parameters |
| SAF latent personalization | Valid SAF model dimensions, feature convention, bounded latent coordinates | Propose bounded latent update; preserve baseline on rejection |
| Room convolution | Explicit user intent or measured/selected room IR; sample rate, channel and provenance metadata | Select and resample a compatible RIR/BRIR; never claim the listener's physical room was measured |
| Room correction | Actual in-room measurement and known playback chain | Estimate correction only over measured/identifiable range |
| Loudspeaker/directivity | Identified loudspeaker, measurement geometry, frequency/angle coverage and calibration provenance | Inform offline analysis or bounded recommendation |
| PA/line-array planning | System geometry, array/splay, venue geometry, audience coverage targets and measurements | Analysis/recommendation only; no blind live rig reconfiguration |
| Content classification | Validated model output and calibrated confidence | Use as one weak contextual signal, never as proof of room geometry or listener preference |
| Preference personalization | Consented, attributable listening-test labels | Update preference model with held-out evaluation |

A capability is unavailable when its required evidence is absent. Genre, artist, a “live” label, device name or generic venue category cannot establish RT60, SPL, room dimensions, speaker model, array geometry or a measured transfer function.

## Decision contract

Input record must include:
- active output route and channel layout;
- sample rate, frame size and latency budget;
- current SAF/DSP state and route-arbitration token;
- requested listening intent, if user supplied;
- available HRTF/SOFA/IHR1/RIR/BRIR/SAF model identifiers and schema versions;
- measured/simulated/estimated/user-declared provenance per feature;
- calibration state, confidence, data age and applicable license/provenance;
- headroom, filter stability, thermal and CPU constraints.

Output record must include:
- selected capability or NO_OP;
- candidate parameter set and parameter bounds;
- reason code and short human-readable rationale;
- evidence IDs and provenance classes used;
- confidence/calibration state and out-of-distribution flag;
- predicted measurable objective change (not a claim of subjective improvement);
- validation result, rollback target and model/policy version.

Required abstention cases: incompatible route, missing required measurements, conflicting metadata, non-finite values, invalid model dimensions, low calibrated confidence, out-of-distribution context, unstable filters, clipping/headroom violation, latency overrun, thermal restriction, or another route already owning the DSP chain.

## Safety and runtime constraints

- Do not allocate, lock, read files, access network, or run training in the real-time audio callback.
- Run bounded inference on a worker; publish immutable validated parameters atomically.
- Never enable Route A and Route B simultaneously.
- Limit parameter magnitude and per-update slew; validate biquad stability and finite values.
- Apply changes through the existing transition/crossfade mechanism. Roll back if the monitor detects a violation.
- Preserve the last known-safe profile on abstention.
- Explain the difference between measured facts, model estimates, simulation and user preferences.

## Data and model plan

1. Audit existing packaged assets and source manifests; emit a deterministic JSON report with checksums, file counts, dimensions, sample rates, channels, coordinate conventions and provenance.
2. Resolve the declared-count discrepancy between the training script and generated master header. Never use header comments as a substitute for scanning the actual corpus.
3. Validate SAF source model dimensions and eigenvalue/component ordering before projecting any HRTF/IHR1 sample into its latent space. Do not assume an HRTF dataset automatically trains room or PA decisions.
4. Add measured SRIR/BRIR, room-enhancement and loudspeaker-directivity corpora only after per-dataset license review. Record source URL, license, attribution, retrieval date, checksum, allowed use, and redistribution restrictions.
5. Preserve labels as measured, simulated, estimated, or user_declared; reject silent label conversion. Heuristic IACC/coherence and hand-authored “Golden Master” priors are not ground truth.
6. Create task-specific training sets: route/capability selection, parameter proposal, and preference ranking are separate targets. Use consented listening-test labels for preference learning.
7. Split by subject, room, recording session and loudspeaker model as relevant. Avoid leakage from multiple measurements of the same subject/room/device into both train and test.
8. Compare learned policies to deterministic rules and no-op baselines. Report macro-F1/confusion matrix, abstention coverage, calibration, held-out acoustic error, clipping/stability, CPU/memory, latency and device thermal behavior.
9. Only after tests pass, integrate inference into SAF orchestration and run A/B listening tests with randomized, level-matched conditions.

## Acceptance gates

- Corpus audit and regression tests pass on real repository assets in CI.
- Source/license manifest complete for every asset used by training or redistribution.
- No unresolved count/schema mismatch between source corpus, generated artifacts and metadata.
- Held-out evaluation is reproducible and grouped to prevent leakage.
- Decision model beats or usefully complements deterministic baseline on declared metrics; confidence is calibrated.
- Abstention and rollback work for missing, conflicting, invalid and out-of-distribution inputs.
- No audio callback allocations/locks/I/O; route arbitration and latency tests pass on target hardware.
- Blind, level-matched listening tests support any subjective sound-quality claim.
- Release is not described as trained, integrated, validated or build-green until the corresponding artifacts and CI/device evidence are linked.
