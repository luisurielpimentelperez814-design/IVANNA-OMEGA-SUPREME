# Moto G85 init_boot / PAL preload audit — 2026-11-04

## Scope

This report records a read-only analysis of the user-supplied active-slot
`init_boot_a.img`, the exact vendor audio service RC, the installed module
scripts, and the PAL HIDL dynamic-section report. It does not modify or flash
the phone, and it does not claim PAL interposition or ASRC activation.

## Active init_boot image

Evidence supplied for the Moto G85 / Android 15 / Magisk 30.7 target:

- Image size: 8,388,608 bytes (8 MiB).
- SHA-256: `f6642cbdea72e1b5927bfae524778071b11215f4b2289e9f91594c8b696c72b2`.
- Android boot image magic: `ANDROID!`.
- Boot header version: 4.
- Kernel payload size: 0 bytes (expected for this init_boot image).
- Ramdisk payload size: 1,940,226 bytes.
- Ramdisk begins at offset 4096 in the supplied image.
- Ramdisk payload uses the legacy LZ4 block magic and decompresses to a
  2,546,176-byte newc CPIO archive containing 26 entries.

The Magisk root overlay in this image contains:

- `overlay.d/sbin/init-ld.xz`
- `overlay.d/sbin/magisk.xz`
- `overlay.d/sbin/stub.xz`

There is **no `overlay.d/*.rc` file** and no IVANNA audio service override
in the supplied ramdisk. The only init RC found inside this ramdisk archive is
`system/etc/init/snapuserd.rc`. This means the currently supplied boot image
does not yet contain an IVANNA early-init RC overlay.

The decompressed `init-ld.xz` is an ELF64 AArch64 Magisk component. Its strings
include `LD_PRELOAD` and `/dev/preload.so`; this is evidence of Magisk's own
internal init-loader mechanism, **not** evidence that it configures
`vendor.audio-hal` or provides a supported IVANNA injection API. Do not
repurpose this internal helper based on strings alone.

## Exact vendor audio service

The supplied
`/vendor/etc/init/android.hardware.audio.service_64.rc` defines:

```rc
service vendor.audio-hal /vendor/bin/hw/android.hardware.audio.service_64
    override
    class hal
    user audioserver
    group audio camera drmrpc inet media mediadrm net_bt net_bt_admin net_bw_acct oem_2901 wakelock
    capabilities BLOCK_SUSPEND SYS_NICE
    ioprio rt 4
    writepid /dev/cpuset/foreground/tasks /dev/stune/foreground/tasks
    onrestart restart audioserver
```

There is no `setenv LD_PRELOAD` in this stanza. The device audit also
reported an active HAL PID and the expected primary HAL / AGM / PAL objects
mapped in that process, but no presence-probe mapping.

## Installed module behavior

The installed `preload_link.sh` is read-only diagnostics:

- enumerates HAL PIDs from `/proc/*/cmdline`;
- checks `/proc/<pid>/maps` for the probe library;
- checks only the `LD_PRELOAD` line in `/proc/<pid>/environ`;
- reports `NOT_LOADED` when the HAL is accessible but the probe is absent.

It does not set the environment or inject into the process.

The installed `service.sh` starts the IVANNA daemon and prepares the ASRC
control/clock directory. It does not configure init's environment for the HAL.
The script itself correctly describes the daemon as the control plane, not
the PCM callback.

## PAL / ASRC boundary

The supplied dynamic-section report for
`vendor.qti.hardware.pal@1.0-impl.so` lists dependencies on
`vendor.qti.hardware.pal@1.0.so` and `libar-pal.so`, and marks the object
`BIND_NOW` / `NOW`. This describes dynamic-linking behavior and dependencies;
it does not prove a PAL hook is active, and it does not by itself prove that
LD_PRELOAD is impossible.

The repository's `PalAsrcWriteStage` is a source-level adapter. It deliberately
leaves construction of the private Qualcomm `pal_buffer` to a target HAL
callback using the exact matching vendor headers. The accompanying HAL audit
states that `StreamOutPrimary::write` copies data into an internal queue and
does not directly call the `pal_stream_write` PLT entry. The queue consumer
and its real PAL write call remain unidentified. The reported PLT references
are candidate callsites, not proof that they carry the active TIDAL PCM.

Therefore the following are still unproven:

1. Which queue consumer drains the active TIDAL stream.
2. Whether the relevant PCM write is local to the primary HAL or crosses into
   the PAL HIDL service.
3. The exact firmware-matched `pal_buffer` ABI and write semantics.
4. A valid paired clock source for the same active stream.
5. An end-to-end runtime test showing processed PCM reaches the existing PAL
   output stream.

## Candidate early-init design — not yet activated

Magisk's documented root-directory overlay supports additional
`overlay.d/*.rc` files in the boot-image ramdisk. Android init documents
`override` as selecting the last parsed service definition with that
modifier. Given the vendor stanza already uses `override`, a later, carefully
ordered duplicate service definition that preserves every OEM option and adds
only a verified `setenv LD_PRELOAD` is a candidate to test.

This is **not yet approved for deployment**. Before building an installable
image, validate all of the following on the exact Android 15 firmware:

1. Prove the Magisk overlay RC is parsed after the vendor service definition.
2. Prove the duplicate service definition wins and preserves the complete
   original service configuration.
3. Prove the preload library exists and is readable at the instant
   `vendor.audio-hal` starts. A late Magisk module mount may be too late for a
   `class hal` service; the path must not be assumed.
4. Prove the Android linker accepts the selected preload path in the
   `audioserver` service context and that SELinux permits access.
5. Keep the probe inert first; confirm the correct PID and mapping before any
   function-level hook is considered.
6. Provide a tested rollback image and a disable path before any flash step.

Do not modify `init_boot`, replace the vendor RC, restart the live audio HAL,
or flash a generated image until these checks are complete.

## Current gates

| Gate | Result |
|---|---|
| Active init_boot image inspected | PASS |
| Existing Magisk overlay layout identified | PASS |
| IVANNA early-init audio RC present | FAIL — absent |
| Target service stanza and OEM options captured | PASS |
| Probe automatically loaded in HAL | FAIL — not observed |
| PAL PCM callsite tied to active stream | BLOCKED |
| Private PAL buffer ABI authenticated | BLOCKED |
| ASRC processing real device PCM | NOT PROVEN |
| Safe installable preload image | NOT BUILT |

## Repository state

PR #27 (presence-only probe) and PR #28 (read-only init_boot evidence
collector) are merged. Their scope is diagnostic/evidence collection; neither
implements a PAL function interposer or activates ASRC.

This report intentionally keeps the project release gate closed until the
runtime and ABI evidence above exists.


## Runtime evidence from the device — 2026-11-04

A subsequent user-supplied runtime report records:

- Magisk `30.7:MAGISK:R`, root path `/debug_ramdisk`.
- `init.svc.vendor.audio-hal=running`.
- `init.svc_debug_pid.vendor.audio-hal=1443`.
- Process command: `android.hardware.audio.service_64`.
- Presence probe exists at both the module overlay path and
  `/vendor/lib64/libivanna_pal_presence_probe.so`.
- Both files are reported as mode `0644`, owner `root:root`, SELinux
  label `u:object_r:vendor_file:s0`.
- Reading `/proc/1443/environ` yielded no visible `LD_PRELOAD=` line and
  the diagnostic printed the combined message
  `LD_PRELOAD ausente o inaccesible`.
- The selected `dmesg` AVC filter returned no matching lines.

Interpretation:

- The HAL service and PID are positively identified.
- The probe is installed in a vendor-labeled location.
- No evidence shows the probe mapped into PID 1443.
- The environment check is intentionally treated as inconclusive because
  its command conflates an absent variable with a denied/unreadable
  `/proc/<pid>/environ`.
- The absence of matching AVC lines in this filtered output does not prove
  that all required SELinux accesses are allowed.

Next non-invasive diagnostic gate: separately test read access to
`/proc/1443/environ` (capture the read exit status), inspect
`/proc/1443/maps` for the exact probe basename, and record the service
process SELinux context. Do not infer preload success from file presence or
the `vendor_file` label.

No runtime injection, service restart, boot-image write, or partition flash
was performed.


## Deep runtime verification — 2026-11-04

The follow-up diagnostic was conclusive for the currently running process:

- PID 1443 process SELinux context: `u:r:hal_audio_default:s0`.
- `/proc/1443/environ` read succeeded (`ENVIRON_READ=OK`).
- The readable environment contained no `LD_PRELOAD=` entry (`LD_PRELOAD_AUSENTE`).
- `/proc/1443/maps` did not contain the exact probe basename
  (`PROBE_NO_MAPEADA`).
- The search under `/debug_ramdisk` found no `*.rc` overlay
  (`OVERLAY RC` section empty).

This removes the earlier ambiguity around procfs access: the environment
was readable, and the preload variable was absent. The probe is not loaded
in the active HAL process. The process is running in
`u:r:hal_audio_default:s0`.

The logcat filter in this diagnostic returned mostly unrelated UI/system
messages and did not provide useful init parser ordering evidence. Therefore
RC import/override order remains unverified. No service restart, live
injection, boot image modification, or flash was performed.

## Updated decision

Do not ship or enable a preload RC yet. The next engineering task is a
static boot/init import-order audit for this exact firmware and Magisk
version, followed by a separate controlled build review. Keep the presence
probe inert. A successful mapping alone would still not prove PAL function
interposition or ASRC PCM processing.
