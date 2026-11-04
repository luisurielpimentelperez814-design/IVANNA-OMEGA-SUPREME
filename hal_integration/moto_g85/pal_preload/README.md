# Moto G85 — IVANNA PAL preload integration

## Status: implementation gated on the active init_boot image

This branch is the integration workstream, not a released or installable PAL
interposer. The existing `omega_pal_presence_probe` is diagnostic-only and
does not connect ASRC or intercept PCM.

### Why the boot image is required

The target is the init service:

```rc
service vendor.audio-hal /vendor/bin/hw/android.hardware.audio.service_64
```

The required environment must exist before init executes that service. A
Magisk module's late-start `service.sh`, a post-boot shell export, or a
systemless mount created after init parsed the vendor rc files cannot change
the environment of the already-created service.

Magisk's root-directory `overlay.d` is part of the boot-image ramdisk. It is
not a normal Magisk module directory. Its `.rc` files are incorporated into
init's root configuration, while module scripts run in later boot stages.
Therefore the exact active Magisk-patched `init_boot` image and its ramdisk
layout must be inspected before choosing a reversible injection point.

### Current device facts (reported for this target)

- Moto G85 / malmo, Android 15, Magisk 30.7
- Active slot previously observed: `_a`
- `init_boot_a` previously observed as 8 MiB
- Vendor init file:
  `/vendor/etc/init/android.hardware.audio.service_64.rc`
- Vendor is EROFS/read-only
- Existing service definition uses `override`

These observations are not a substitute for inspecting the current image.
The collector below reads the active init_boot partition and the exact vendor
service rc. It does not write to any partition, restart a service, or change
SELinux.

### Collect evidence

Run as root on the target phone:

```sh
su -c 'sh /path/to/collect_init_boot_evidence.sh'
```

The script writes a ZIP under `/sdcard/Download/`. Share that ZIP for the
next implementation stage. It contains the active init_boot image, its SHA256,
the exact audio service rc, and minimal boot/Magisk metadata. Review it before
sharing; it is a boot image from your device.

### Release gates

A release ZIP must not be produced until all of these are true:

1. The active init_boot ramdisk is unpacked and its Magisk overlay layout is
   identified.
2. The chosen early-init configuration is proven to be parsed after the
   vendor service definition, or the exact service definition is safely
   replaced without losing OEM options.
3. Existing `LD_PRELOAD` entries, if any, are preserved.
4. The library ABI and all intercepted PAL function signatures are verified
   against this firmware. No guessed `pal_buffer` or timestamp ABI.
5. The module has an explicit disable/rollback path and passes ZIP/ELF/static
   validation.
6. Device validation confirms the probe is mapped in the actual HAL PID.
   Presence alone does not prove TIDAL's PCM path or ASRC activation.

Until those gates pass, this branch must not claim automatic injection, PAL
interposition, ASRC activation, or improved sound.
