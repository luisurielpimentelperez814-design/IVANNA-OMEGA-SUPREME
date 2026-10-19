# FotoHolo Acoustic Knowledge & Consent Architecture

**Knowledge pack:** `src/lib/fotoholoAcousticKnowledge.ts`  
**Version:** 1.0.0  
**Status:** curated engineering guidance wired into the visual-refinement prompt; not a trained neural model, not an exhaustive global database, and not yet a validated autonomous room-correction engine.

## What is now embedded

- 26 selectable music intents spanning 1970s/1980s/1990s rock, classic/hard rock, punk, indie, pop, hip-hop, R&B/soul/funk, jazz, classical/opera, electronic/house/techno, ambient, reggae/dub, Latin, regional Mexican, reggaeton, Afrobeats/amapiano, folk/world, gospel, speech/podcast, cinema/gaming and unknown.
- Eight listening environments: headphones, nearfield, living room, treated studio, club, arena/mass concert, theatre/elite room, outdoor and unknown.
- Output-route guidance for built-in speaker, 3.5 mm analog, USB-C, USB audio, Bluetooth and unknown.
- Engineering guardrails: measurement before room correction; distinguish DSP, transducer/system and room; preserve dynamic range and artistic intent; limit proposals; don't fabricate SPL, RT60, dimensions, anatomy or hardware response.
- The web Gemini endpoint now receives allowlisted genre/venue guidance from the local knowledge pack, user-entered optional track context, and only recent aggregate telemetry. The Android Gemini bridge receives bounded genre/venue context and the same visual-only restriction.
- The knowledge pack and version are authored in code. This is retrieval/context injection into a prompt, **not model training or a claim that a neural network has memorized all acoustic knowledge**.

## Data and sensor acquisition boundaries

### Music identity / platform metadata

1. First use metadata supplied by the user or OS media-session metadata when the platform exposes it and permissions/policies permit.
2. A provider connector must be independently authorized, display attribution where required, and store only fields needed for the feature.
3. Do not scrape pages, intercept/decrypt streams, bypass DRM, or copy full tracks into a model prompt. Provider rules change and require per-provider legal/policy review.
4. **Spotify-specific block:** Spotify's current Developer Policy prohibits using its platform/content to train or ingest into AI models and restricts applications integrated with streams/content from another service. Therefore Spotify-derived metadata/content must not be sent to Gemini or used as AI input absent explicit written authorization and a policy review. The UI's title/artist fields are optional, user-entered context, not an automatic Spotify/YouTube/Tidal/Qobuz connector.

### Microphone / playback signal

- Microphone room calibration must be an explicit, visible session with Android runtime permission, a start/stop control, a clear indicator, and a discard option. Process locally by default; no raw recording upload by default.
- A phone microphone is not calibrated SPL measurement without calibration data. Do not claim absolute SPL, RT60 or frequency-response correction from uncalibrated samples.
- Android playback capture of another app requires OS-mediated MediaProjection consent, RECORD_AUDIO permission and a source app that allows capture. It cannot be promised for every streaming app.
- Never record ambient speech or bystanders as a hidden always-on process. Store short-lived aggregate features unless the user expressly saves a calibration.

### Network, room geometry and people

- Wi-Fi scans do not measure the room's physical dimensions, loudspeaker response or the anatomy of people present. Wi-Fi/local-network inspection is not a valid substitute for room measurement.
- Do not scan unrelated LAN devices or identify attendees. Any local-device discovery must be feature-specific, permissioned, disclosed, minimal and user initiated.
- For optional geometry, use a user-triggered camera/depth/AR measurement flow (if device hardware supports it), show the scan preview and uncertainty, process locally, and delete it after calibration unless the user opts to save it.
- Do not infer or store body/anatomical or biometric profiles of bystanders. If personalized spatial audio is later added, offer a voluntary self-calibration to the listener, explicit consent, local processing and deletion controls.

## Safe adaptive control roadmap

Current implementation refines the **visual scene** only. Actual autonomous DSP changes are a separate gated stage and must not be claimed as implemented yet.

1. Add a typed, timestamped sensor snapshot with validity and calibration metadata.
2. Estimate only what the measurement supports: spectral balance, clipping, transients, route, sample rate, buffer/latency and underruns; calibrated sweep/IR is required for room impulse metrics.
3. Generate a bounded proposal from the acoustic rules and telemetry, with confidence and rationale.
4. Run a dry-run / before-after simulation; enforce master-gain and hearing-safety limits, DSP stability, headroom, and rate-of-change limits.
5. Require explicit user opt-in for an adaptive mode, show a visible indicator, log every parameter change, offer immediate bypass/undo, and never change master volume upward automatically.
6. Validate with physical loopback and measurement microphones across headphones, analog, USB and Bluetooth. Do not automatically apply room EQ based only on song genre or song title.

## References and rights

- AES, Sound Reinforcement and Live Sound System Optimization: https://aes.org/audio-topics/sound-reinforcement/
- AES, standards and technical resources (copyrighted standards must be obtained and used under their terms): https://aes.org/standards/ and https://aes.org/publications/standards-store/
- ITU-R BS.1116-3, subjective assessment of small audio impairments: https://www.itu.int/rec/R-REC-BS.1116/en
- Android, playback capture and MediaProjection consent: https://developer.android.com/media/platform/av-capture
- Android, Wi-Fi permissions and scanning limits: https://developer.android.com/develop/connectivity/wifi/wifi-permissions and https://developer.android.com/develop/connectivity/wifi/wifi-scan
- Spotify Developer Policy, effective 15 May 2025: https://developer.spotify.com/policy
- Spotify Get Track API and attribution/policy notes: https://developer.spotify.com/documentation/web-api/reference/get-track

This knowledge pack paraphrases engineering principles and links to primary sources; it does not republish proprietary AES standards or train on protected music.
