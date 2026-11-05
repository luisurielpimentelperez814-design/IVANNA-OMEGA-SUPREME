# Moto G85 init_boot / PAL preload audit — 2026-11-04

## Scope

**Target identity correction (2026-11-05):** the owner confirms the Moto G85 currently runs Android 14 / SDK 34. The Android 15 / SDK 35 identity and image hash recorded below (`f6642...`) belong to a separate earlier evidence set and are not authoritative for the current target. The separately supplied `ivanna_init_boot_a.img` analyzed in `docs/device_audits/MOTO_G85_INIT_BOOT_AUDIT.md` has SHA-256 `db3dd9987183fb843b7b274f43dd7ec66a7670d737853df6d00f24361759e9e1` and identifies Android 14 / SDK 34. Do not apply Android 15-specific init ordering conclusions from this historical report to the Android 14 device without revalidating against Android 14 init sources and the current vendor RC.

This report records a read-only analysis of the user-supplied active-slot
`init_boot_a.img`, the exact vendor audio service RC, the installed module
scripts, and the PAL HIDL dynamic-section report. It does not modify or flash
the phone, and it does not claim PAL interposition or ASRC activation.

## Active init_boot image

Historical evidence set (not the current owner-confirmed Android 14 target), previously labeled Moto G85 / Android 15 / Magisk 30.7:

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


## Historical init import-order finding — Magisk v30.7 / Android 15 (not validated for current Android 14 target)

The earlier candidate suggesting that a top-level `overlay.d/*.rc` could
provide a later duplicate `vendor.audio-hal` service definition is withdrawn.
Inspection of the exact Magisk v30.7 source tag
(`native/src/init/rootdir.cpp`) shows:

1. `load_overlay_rc()` reads top-level `overlay.d/*.rc` entries. New RC
   files are collected into `rc_list`; existing rootdir RC paths are handled
   as replacements. `overlay.d/init.rc` is explicitly not accepted as an
   overwrite.
2. `patch_rc_scripts()` appends `rc_list` to the primary
   `/system/etc/init/hw/init.rc` (Android 11+ path) and then injects Magisk's
   own RC entries.
3. AOSP Android 15 init documentation says the primary init RC is parsed
   before the partition init directories, including `/vendor/etc/init/`.
   Therefore a service definition added by Magisk's top-level overlay RC is
   parsed before the Motorola vendor service definition.
4. The exact Motorola service stanza already contains `override`. Android
   init specifies that the last parsed definition carrying `override` wins.
   The later vendor stanza therefore takes precedence over an earlier
   overlay-added duplicate.

Consequently, a simple `overlay.d/*.rc` duplicate service stanza cannot be
claimed to override this vendor service. The previous candidate design and
the corresponding release gate wording are superseded by this finding.

### Why the obvious fallback is not approved

Android init's `export NAME VALUE` command sets a global init environment
variable inherited by all subsequently started child processes. It is not
service-scoped like the `setenv` service option. Setting global
`LD_PRELOAD` early could affect many unrelated init-launched processes;
this is not an acceptable substitute for a targeted HAL service environment
without a separately designed and tested process-filtering architecture.
No global `LD_PRELOAD` action is proposed.

A later parsed service override would need to be available in an init RC
source parsed after the vendor definition (for example, an actual later
partition init source) or the original vendor service definition itself
would need a controlled replacement. The current evidence does not establish
a safe, early, reversible way to provide either on this device. A normal
Magisk module `service.sh` or post-fs-data file mount is too late to change a
service definition already parsed by init.

### Current engineering decision

- Do not add a duplicate `vendor.audio-hal` stanza to `overlay.d/*.rc`.
- Do not set global `LD_PRELOAD` in init.
- Do not modify or flash `init_boot` on this basis.
- Keep the presence probe inert and the PAL/ASRC release gates closed.
- Continue by checking the device's effective primary init path and actual
  partition init directories, then investigate whether the firmware exposes
  a supported later init source or whether a vendor-image-level change is
  unavoidable.

### Source references

- Magisk v30.7 source tag:
  `native/src/init/rootdir.cpp`,
  `load_overlay_rc()`, `patch_rc_scripts()`, and `patch_ro_root()`.
- AOSP Android 15 init README (android15-qpr2-s9-release):
  primary init RC parsing and partition-directory import order; service
  `override` semantics.
- AOSP init language documentation:
  `export` is global and inherited by subsequently started processes.

This correction is based on source-level boot/init behavior, not a live
modified-boot test. The exact target boot flow should still be checked against
the device's effective `ro.boot.init_rc`, mounts, and parsed init files.


## Target-device init-order audit — 2026-11-04

Runtime evidence collected from `IVANNA_INIT_ORDER_AUDIT.txt` on the Moto G85:

- Build: `motorola/malmo_gn/malmo:15/V1UOS35H.74-16-4-16/11943-1eca7:user/release-keys`.
- Magisk: `30.7:MAGISK:R`.
- `ro.boot.init_rc` was empty; the collector therefore inspected the fallback
  primary RC `/system/etc/init/hw/init.rc`.
- The primary RC imports `/vendor/etc/init/hw/init.${ro.hardware}.rc`
  and later executes `class_start hal` (line 1271 in the captured file).
- The top-level vendor init directory contains both
  `android.hardware.audio.service.rc` (32-bit binary) and
  `android.hardware.audio.service_64.rc` (64-bit binary).
- The 64-bit stanza declares `service vendor.audio-hal
  /vendor/bin/hw/android.hardware.audio.service_64` and includes
  `override`; the 32-bit stanza points to
  `/vendor/bin/hw/android.hardware.audio.service` and has no `override`.
- `/odm/etc/init` does not exist. The collector found no matching
  `LD_PRELOAD` or duplicate service stanza in the scanned top-level
  `/system/etc/init`, `/system_ext/etc/init`, `/vendor/etc/init`, or
  `/product/etc/init` files.
- `/vendor`, `/system_ext`, and `/product` are mounted EROFS read-only.
- The active service is `running`, PID `1443`.
- `/debug_ramdisk/overlay.d` contained no files in the captured scan.

### What this establishes and what remains open

This confirms the actual 64-bit service definition and the absence of a
visible top-level ODM init directory or obvious later duplicate in the
directories scanned. It does not prove that every possible later RC source
was inspected: the collector listed only max-depth-1 files in each init
directory and did not enumerate the explicitly imported
`/vendor/etc/init/hw/init.${ro.hardware}.rc` file or nested init paths.
The value of `ro.hardware` was not included in the capture, so that imported
filename cannot yet be resolved from this report.

### Next read-only evidence gate

Resolve `ro.hardware`, inspect the exact imported vendor hardware RC, and
recursively enumerate init RC files under `/system/etc/init`,
`/system_ext/etc/init`, `/vendor/etc/init`, and `/product/etc/init`.
For each file, capture only occurrences of `vendor.audio-hal`,
`LD_PRELOAD`, and imports that could introduce additional RC definitions.
Do not modify any file, mount, boot image, or service while collecting this.

Until this missing import path is inspected, the finding is: no supported
later service override has been identified—not that one is categorically
impossible. No preload mechanism is approved and no physical PAL/ASRC
activation is claimed.


## Deep target import result — init.qcom.rc

The follow-up device capture `IVANNA_INIT_ORDER_DEEP.txt` resolves
`ro.hardware=qcom` and confirms the primary vendor hardware import is
`/vendor/etc/init/hw/init.qcom.rc`.

That file exists and explicitly imports, in order:

- `/vendor/etc/init/hw/init.qti.ufs.rc`
- `/vendor/etc/init/hw/init.qcom.usb.rc`
- `/vendor/etc/init/hw/init.qcom.test.rc`
- `/vendor/etc/init/hw/init.target.rc`
- `/vendor/etc/init/hw/init.qcom.factory.rc`

The captured recursive grep found no `LD_PRELOAD` or explicit
`vendor.audio-hal` service definition in those matching RC files under the
scanned init roots; its only `vendor.audio-hal` matches were service
restart/start references in `/system/etc/init/audioserver.rc`.
However, the capture did not print the import directives of each nested RC
file, nor enumerate every imported descendant. Therefore the remaining
static audit is to follow the above import graph recursively, not to infer
that those imported files cannot introduce additional RC sources.

Current status: no later, supported service override is identified. The
exact qcom hardware RC and its first-level import graph are now known. No
boot image or service was modified; preload and real PAL/ASRC activation
remain unproven and disabled.
