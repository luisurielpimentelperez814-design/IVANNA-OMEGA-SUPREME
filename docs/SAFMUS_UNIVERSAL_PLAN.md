# SAFMUS Universal — research and integration plan

Status: **research specification; not a validated universal model**.
Branch: `research/safmus-universal-audit`.

## Findings from the existing repository

The repository already contains substantial acoustic infrastructure. Do not create a parallel engine.

- `scripts/train_sofa_saf_rir_master.py` claims to combine SOFA, IHR1, a SAF PCA model and packaged RIR WAVs, then emits `pca_basis.bin` and `SofaSafRirMasterKnowledge.hpp`.
- `app/src/main/cpp/spatial/RirDataset.cpp` loads room metadata, selects RIRs and resamples impulse responses.
- `app/src/main/cpp/spatial/SofaSafRirMasterKnowledge.hpp` contains generated tensors.
- `app/src/main/java/com/ivanna/omega/assistant/IvannaAudioKnowledgeBase.kt` is a rule/glossary layer, not a learned acoustic policy.
- `src/lib/fotoholoAcousticKnowledge.ts` already distinguishes headphone, nearfield, living-room, studio, club, arena, theatre and outdoor contexts, and explicitly says not to infer room response without measurement.
- Existing code comments describe TinyML classification into voice/music/bass/silence. That classification is not equivalent to a validated model that chooses the correct acoustic intervention for every venue and system.

### Critical audit issue

In `scripts/train_sofa_saf_rir_master.py`, the room-analysis path derives early/late IACC from heuristics and a binaural-coherence manifold for near-coincident stereo RIRs. These values are estimates, not directly measured binaural IACC. SAFMUS must preserve provenance and must not label such estimates as measurements or train them as ground-truth labels without explicit qualification.

The training script also contains hand-authored "Golden Master" latent priors. These are design assumptions, not experimentally demonstrated universal preferences. Keep them separate from data-derived statistics and user preference labels.

## SAFMUS architecture

Use a **context router + specialist estimators + constrained optimizer**, not one indiscriminate model.

1. **Context and route detector**
   - Inputs: active audio route, sample rate, channel count, DSP path, user-selected context and available sensor/calibration state.
   - Never infer speaker layout, room dimensions, calibrated SPL or RT60 from genre alone.
   - Distinguish headphone/binaural, nearfield/stereo, measured room correction, PA/line-array planning, and creative room-effect rendering.
2. **Evidence ledger**
   - Every feature carries source, units, acquisition method, confidence, timestamp/version and license/provenance where applicable.
   - Separate measured, simulated, estimated and user-declared values.
3. **Specialist models**
   - HRTF/HRIR and head tracking.
   - RIR/BRIR and room acoustics.
   - Loudspeaker response/directivity.
   - PA/line-array coverage and alignment (requires system geometry and measurement; no blind live reconfiguration).
   - Content/source classification.
   - Perceptual preference model, trained only on consented and valid listening-test labels.
4. **Decision policy**
   - Select only specialists compatible with the active route and evidence.
   - Produce a proposal plus confidence, expected effect, constraints and explanation.
   - Abstain and preserve the previous safe profile when data are missing, conflicting or out of distribution.
5. **Constrained optimizer**
   - Minimize weighted measurement residual plus smoothness, magnitude and temporal-change penalties.
   - Enforce headroom, bounded EQ, stable filters, latency and user/system limits.
   - Validate a candidate before crossfading it into the active DSP path.
6. **Runtime separation**
   - Inference and training-data preparation must not run on the real-time audio callback.
   - Run bounded inference on a worker; publish immutable validated parameters atomically to the audio engine.
   - Never enable a second processing route when Route A/Route B arbitration says one is already active.

## Proposed objective

For context-specific observation vector y, model h(x; c), correction q and confidence weights W:

\[
J(x,q\mid c)=\rho_W(y-h(x;c)-B_cq)
+\lambda_x\|x\|^2+\lambda_s\|D^2q\|^2
+\lambda_q\|q\|^2+\lambda_t\|q-q_{prev}\|^2
+\lambda_p L_{pref}(q)
\]

Here c is the validated context, x contains only identifiable model parameters, and B_c is a context-specific local response model. The preference term is omitted unless actual preference labels exist. Optimize with bounded/damped steps and accept only if a measured or simulated validation objective improves. The objective is a proposed research formulation, not a proven universal law.

## Dataset acquisition priorities

See `research/safmus/dataset_sources.json`. Start with metadata and license review, then download only sources whose terms permit the intended use. Do not merge sample arrays across incompatible conventions without conversion records. Do not redistribute raw datasets or derived artifacts where licenses prohibit it.

Priority order:
1. Audit all existing packaged SOFA, IHR1, RIR, model and metadata files.
2. Add measured SRIR/BRIR, room-enhancement and loudspeaker directivity datasets.
3. Record provenance, license, calibration, channels, geometry, sample rate, measurement vs simulation, and subject/venue split.
4. Build reproducible preprocessing with per-file validation and checksums.
5. Train specialist models with splits by subject, room and loudspeaker model to prevent leakage.
6. Compare to simple baselines and report held-out errors, confidence calibration, latency, CPU/memory and listening-test results.

## Acceptance gates

- Corpus audit exits non-zero for malformed manifests, missing files referenced by metadata, invalid sample rates, invalid numeric values, or schema drift.
- Measured and inferred labels are never silently mixed.
- Held-out evaluation splits by subject AND room/device as applicable.
- Route-selection confusion matrix and abstention rate are reported.
- EQ is bounded and stable; headroom and clipping tests pass.
- Audio-thread allocation/locks/I/O remain zero.
- No claim of "trained", "universal", "better sounding" or "green build" until the corresponding training, blind listening and CI evidence exists.
