# Moto G85 audio-module consolidation

The main Magisk package is `ivanna_omega_supreme`. The four legacy modules
below duplicate or conflict with its PAL diagnostics / audio service setup:

- `ivanna_audio_override` — adds a competing `vendor.audio-hal` override and
  requests the nonexistent `libivanna_pal_probe.so`.
- `ivanna_hal_fix` — adds another `vendor.audio-hal` override and requests
  `libaudio_probe.so`.
- `ivanna_audio_probe` — legacy wrapper/property-based preload probe.
- `omega_pal_presence_probe` — standalone copy of a presence-only probe now
  packaged by the main module.

## Migration

After installing a verified main-module build, inspect:

```sh
su -c 'sh /data/adb/modules/ivanna_omega_supreme/audio_unify.sh status'
```

Apply the consolidation for the next boot:

```sh
su -c 'sh /data/adb/modules/ivanna_omega_supreme/audio_unify.sh apply'
```

Undo only the disable markers created by this migration:

```sh
su -c 'sh /data/adb/modules/ivanna_omega_supreme/audio_unify.sh rollback'
```

The script records exactly which modules it disabled and does not re-enable
modules that were already disabled before migration. It only creates/removes
Magisk `disable` markers; reboot is required for the module mount state to
change. It does not modify vendor files, init rc files, boot partitions,
SELinux, the live HAL process, or `LD_PRELOAD`.

## Explicit safety boundary

This consolidation removes duplicate legacy configuration; it does **not**
activate a PAL interposer or physical ASRC. The main module's
`libivanna_pal_presence_probe.so` is diagnostic-only. Early-init injection
still requires inspection of the active patched `init_boot`, confirmation of
the correct parse order and OEM service options, verified PAL ABI/signatures,
and a tested rollback. A successful migration must never be described as
proof that PCM passes through ASRC.
