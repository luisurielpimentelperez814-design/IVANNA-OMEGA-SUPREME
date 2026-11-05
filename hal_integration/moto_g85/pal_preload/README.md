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

### Current device facts (owner-confirmed target)

- Moto G85 / malmo, Android 14 / SDK 34
- The supplied `ivanna_init_boot_a.img` identifies build `UUOS34HV-V1-ST11.1` and fingerprint `motorola/malmo_g/malmo:14/UUOS34HV-V1-ST11.1/132c31:user/release-keys`.
- Active slot `_a` and 8 MiB image size were previously observed; verify them again before any write operation.

A separate older diagnostic snapshot in repository notes was labeled Android 15 / SDK 35. It is historical and must not be treated as the current target or used to infer this Android 14 init import order.
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

## Estado en el modulo principal (v2.5.341)

El modulo principal `ivanna_omega_supreme` ahora **genera y empaqueta**
`libivanna_pal_presence_probe.so` en `system/vendor/lib64/` (Magisk la monta en
`/vendor/lib64/`). La compila el CI desde `../pal_preload_probe/` y verifica:
AArch64, sin `libc++_shared`, ningun simbolo `pal_*`/`agm_*` definido ni
requerido, segmentos LOAD alineados a 16 KB, y que el ZIP contenga exactamente la
libreria compilada (`BUILD_INFO: preload_probe_sha256`).

Lo que **no** cambia, y por que:

- **LD_PRELOAD sigue sin estar activo.** Ningun script del modulo exporta
  `LD_PRELOAD`, escribe `setenv` ni reinicia el HAL (lo fija un test). init ya
  creo `vendor.audio-hal` antes de que corra `service.sh`, y el rc del vendor es
  EROFS de solo lectura. Los gates 1-6 de arriba siguen vigentes.
- **No hay hook de PAL ni ASRC.** La libreria es solo de presencia.

Para saber que esta pasando realmente en el dispositivo (root):

```sh
su -c 'sh /data/adb/modules/ivanna_omega_supreme/ivanna_control.sh preload'
```

Lee `/proc/<pid>/maps` y `/proc/<pid>/environ` de los procesos
`android.hardware.audio.service*` y devuelve un `verdict=`:

| verdict | significado |
|---|---|
| `LIB_NOT_INSTALLED` | el zip instalado no trae la sonda (anterior a v2.5.341) |
| `HAL_NOT_RUNNING` | no hay proceso del HAL que inspeccionar |
| `NEEDS_ROOT` | hay HAL pero `maps` no se pudo leer; ejecutar con `su -c` |
| `NOT_LOADED` | HAL activo y la sonda no esta mapeada (**esperado hoy**) |
| `LOADED` | mapeada en el HAL; no prueba PCM de TIDAL ni ASRC activo |

`preload report` imprime el reporte constructor-time si el HAL llego a escribirlo.
El reporte exige permiso SELinux de escritura para el dominio del HAL, que este
modulo no concede porque ese dominio no esta verificado en este firmware; el
veredicto por `/proc` no depende de el.

### Siguiente paso real

Ejecutar `collect_init_boot_evidence.sh` (mas arriba) y compartir el ZIP. Con el
`init_boot` activo y el rc exacto del servicio se puede decidir el punto de
inyeccion en early-init (gates 1-3), y despues validar con `preload` que el
veredicto pase de `NOT_LOADED` a `LOADED` (gate 6).
