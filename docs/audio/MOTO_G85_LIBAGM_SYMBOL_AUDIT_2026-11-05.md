# Moto G85 — libagm.so dynamic-symbol audit (2026-11-05)

## Provenance and safety boundary

This record is derived from the two user-supplied text exports `libagm.so.dynamic.txt`
and `libagm.so.symbols.txt`. The supplied exports do not include the binary,
SHA-256, ELF Build ID, firmware fingerprint, or capture timestamp from the device.
Therefore the addresses below are file-relative ELF symbol values from that
specific export, **not runtime addresses** and not yet authenticated against
the current Moto G85 Android 14 / SDK 34 installation.

Do not hard-code these values into a hook, patch, or native link. Resolve exported
symbols by name in the loaded module and verify the exact library identity first.
Small exported function sizes (4 or 8 bytes) are likely veneers/thunks; follow
their AArch64 branch target before assigning implementation semantics.

## ELF dynamic section

- SONAME: `libagm.so`
- `BIND_NOW` and `DF_1_NOW` are set.
- Needed libraries: `libar-gsl.so`, `liblog.so`, `liblx-osal.so`,
  `libaudioroute.so`, `libats.so`, `libtinyalsa.so`,
  `libaudio_log_utils.so`, `libexpat.so`, `libc++.so`, `libc.so`,
  `libm.so`, `libdl.so`.
- The dynamic symbol table contains 313 entries; the supplied table exposes
  AGM, graph, device, session, timestamp, and buffer APIs.

## Relevant exported symbols

Values and sizes are copied from the supplied `.dynsym` listing:

| Symbol | ELF value | Size | Integration relevance |
|---|---:|---:|---|
| `agm_init` | `0x26aa0` | 36 | AGM lifecycle |
| `agm_deinit` | `0x26aa4` | 36 | AGM lifecycle |
| `agm_session_open` | `0x264d0` | 4 | Session creation |
| `agm_session_prepare` | `0x26800` | 40 | Session preparation |
| `agm_session_start` | `0x26804` | 40 | Session start |
| `agm_session_stop` | `0x26808` | 40 | Session stop |
| `agm_session_close` | `0x2680c` | 40 | Session teardown |
| `agm_session_write` | `0x264f0` | 8 | Candidate PCM/session write boundary; thunk must be followed |
| `agm_session_read` | `0x264f4` | 8 | Session read boundary |
| `agm_session_set_params` | `0x26478` | 12 | Parameter transport; IDs and payload schema still need proof |
| `agm_session_get_buf_info` | `0x26540` | 4 | Buffer metadata query |
| `agm_get_session_time` | `0x26520` | 4 | Session timestamp candidate |
| `agm_get_buffer_timestamp` | `0x26530` | 4 | Buffer timestamp candidate |
| `agm_session_flush` | `0x26814` | 40 | Session lifecycle |
| `graph_open` | `0x265e0` | 4 | Graph lifecycle |
| `graph_prepare` | `0x26704` | 36 | Graph lifecycle |
| `graph_start` | `0x26708` | 36 | Graph lifecycle |
| `graph_stop` | `0x26650` | 12 | Graph lifecycle |
| `device_open` | `0x26b20` | 32 | Hardware endpoint lifecycle |
| `device_prepare` | `0x26b24` | 32 | Hardware endpoint lifecycle |
| `device_start` | `0x26b28` | 32 | Hardware endpoint lifecycle |

The API export names establish callable entrypoints, not their private argument
layouts, timestamp units, stream-to-session mapping, or hardware side effects.
In particular, `agm_session_set_params` proves a parameter transport exists;
it does not identify a variable-ratio ASRC parameter.

## Repository integration performed

`tools/diag/hook_pal_agm.js` now passively observes the AGM session write/read,
session lifecycle, graph lifecycle, device lifecycle, and timestamp/buffer-info
entrypoints when those exports are present in the loaded `libagm.so`.

The new AGM observer records only opaque argument register values and return
codes. It deliberately does not dereference AGM pointers, decode structs, write
parameters, alter return values, intercept PCM, or modify mixer controls.
The hot write observer is capped at 300 calls per process to limit log volume.

## Required next capture on the exact Android 14 build

1. Capture `sha256sum`, ELF Build ID, SONAME, and fingerprint for the live
   `/vendor/lib64/libagm.so` (or its actual mapped path).
2. Attach the passive Frida observer to the actual process that maps
   `libagm.so`, before starting one known playback stream.
3. Correlate `agm_session_open/start/write` with PAL stream handle, process PID,
   active format, and the same playback interval.
4. Follow the branch target of `agm_session_write` and recover the exact
   argument ABI from its implementation and callers.
5. Trace `agm_get_session_time` / `agm_get_buffer_timestamp` through the
   success path; establish units and whether frame count and timestamp are
   sampled atomically for the same output stream.
6. Identify a documented or firmware-proven ASRC ratio actuator, parameter ID,
   payload schema, units, bounds, and successful read-back before connecting
   the existing drift servo.

## Acceptance boundary

This commit closes the **symbol inventory and passive AGM observability gap**.
It does not claim live HAL injection, TIDAL PCM interception, hardware-correlated
clock publication, physical ASRC actuation, or audible improvement. The
existing ASRC ratio must remain unity for the proprietary PAL/AGM output path
until both the paired clock source and actuator are verified on this exact
firmware. Keep the existing reversible deployment and rollback gates intact.
