# Moto G85 — PAL LD_PRELOAD presence probe

This is a presence-only diagnostic, not the ASRC hook and not a PCM
interceptor. It proves whether LD_PRELOAD loaded into a process and records
which audio/PAL/AGM objects and PAL symbols are visible in that process.

## Safety

- Defines no pal_* symbol; it does not interpose or replace PAL calls.
- Does not read or write PCM, PAL buffers, stream handles or timestamps.
- Does not change audio arguments, return values, mixer state or SELinux.
- Does not restart the audio service.
- Performs one constructor-time snapshot, not work on the audio callback.
- If the report cannot be written, it logs an error and returns.

A report proves only that the probe loaded in that process. It does not prove
that TIDAL PCM passes through that process or that ASRC is connected.

## Build output

GitHub Actions builds libivanna_pal_presence_probe.so for arm64-v8a and
packages Omega_PAL_Presence_Probe_MotoG85.zip.

The Magisk ZIP installs:
 /vendor/lib64/libivanna_pal_presence_probe.so

The module is intentionally inert until the existing audio service init
definition is explicitly configured. service.sh does not inject into or
restart the HAL.

## Explicit activation — manual and reversible

1. Identify the exact active audio service stanza in vendor/odm init .rc files.
   Do not guess its service name or executable.
2. Back up that exact .rc file.
3. Add this line inside the existing target service stanza, before its exec:

   setenv LD_PRELOAD /vendor/lib64/libivanna_pal_presence_probe.so

   If LD_PRELOAD already exists, append this library to its colon-separated
   value. Do not discard existing entries.
4. Reboot. Do not ctl.restart the audio HAL during playback.
5. Play TIDAL for one minute and inspect:
   /data/vendor/ivanna_audio/pal_preload_presence.txt
6. Match the report PID to the actual HAL/PAL process and inspect visible
   symbols and mapped objects.

Android init reads service environment directives from its service definition.
A Magisk service.sh export cannot retroactively change the environment of an
already-running init service. This module does not pretend otherwise.

## Rollback

- Remove only the added setenv entry from the backed-up init file.
- Disable/remove omega_pal_presence_probe in Magisk.
- Reboot.
- Preserve any pre-existing LD_PRELOAD entries.

## Not included

No pal_get_timestamp hook, no pal_stream_get_mmap_position hook, no
pal_stream_write interception, no clock producer, no ASRC processing. Those
require a verified call path, target ABI and a separately reviewed hook.
Public Qualcomm headers are references, not proof of this firmware's ABI.
