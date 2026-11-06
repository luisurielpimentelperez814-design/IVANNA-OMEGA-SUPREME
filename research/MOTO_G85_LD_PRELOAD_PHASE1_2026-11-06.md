# Moto G85 — LD_PRELOAD en vendor.audio-hal: Informe de Fase 1 (2026-11-06)

Estado: **investigación**. No se modificó ningún dispositivo, partición ni archivo de `/vendor`.
La sonda **no** está demostrada cargada y **no** hay ASRC funcional: este informe no afirma ninguna de las dos cosas.

## 1. Qué se investigó
1. Política SELinux viva del dispositivo (`fabab13d-sepolicy.bin`, SHA-256 `47bceb73…bd34`).
2. Estado de PR #24, #25 y #35 y de los comentarios de revisión de #25 contra el código.
3. Mecanismos de inyección (orden de RC, `overlay.d`, `wrap.*`).

## 2. Hallazgos con evidencia

### 2.1 Política SELinux (parser propio `tools/selinux/sepolicy_query.py`)
El parser se autovalida: leyó 2 525 364 de 2 525 364 bytes (policydb v30, 3 965 tipos, 170 754 reglas allow).
Es la política viva con parche Magisk (existen `magisk_file`, `magisk_log_file`; **no existe ningún tipo `ivanna*`**).

| Consulta | Resultado |
|---|---|
| `allow init hal_audio_default:process` | `getpgid rlimitinh siginh sigkill signal transition` — **sin `noatsecure`** |
| `init → X:process noatsecure` en toda la política | un solo destino: `magisk`. Ningún dominio HAL (audio, power, health, graphics), zygote, vold ni audioserver |
| `hal_audio_default → vendor_file:file` | `execute getattr map open read` (puede cargar una .so con etiqueta `vendor_file`) |
| `hal_audio_default → system_file:file` | sin reglas (una .so con etiqueta `system_file` NO se podría mapear) |
| `hal_audio_default → adb_data_file:file` | sin reglas (cargar desde `/data/adb/modules/...` directamente NO es posible) |
| `hal_audio_default → magisk_file:file` | permisos amplios (incluye `execute map`) |

**Consecuencia (hipótesis fuerte, por verificar en el dispositivo):** sin `noatsecure`, la transición `init → hal_audio_default`
ocurre con `AT_SECURE=1`. En bionic, con `AT_SECURE` las variables inseguras (incluida `LD_PRELOAD`) se descartan.
Por tanto **ninguna forma de poner `LD_PRELOAD` en el servicio funcionaría** mientras esa regla falte. Esto es compatible con
la sonda actual (nunca cargó) y es independiente de `wrap.*` o del orden de RC.
Condiciones para verificarlo: leer `AT_SECURE` en `/proc/<pid>/auxv` del HAL (el script lo hace; el `od ... | awk '$1==23'` de tu
prueba anterior no imprimió línea alguna, así que **esa prueba fue inconcluyente**; el script usa `od -An -tu8 -v -w16`).

Regla candidata (NO aplicada): `allow init hal_audio_default process noatsecure` en `sepolicy.rule` del módulo. Efecto: solo
afecta a futuros `exec` de ese dominio desde init; riesgo bajo; no debilita al propio HAL; Magisk la carga en `post-fs-data`.
Reversible quitando la línea y reiniciando.

### 2.2 Ubicación de la etiqueta de la biblioteca
Para mapearla dentro del HAL, `libaudio_probe.so` debe verse como `vendor_file` (o `vendor_file_type`). Si el mount de
Magisk la deja como `adb_data_file` o `system_file`, el linker fallará aunque `AT_SECURE` se resuelva. El script reporta
`ls -lZ /vendor/lib64/libaudio_probe.so`.

### 2.3 Orden de RC (HIPÓTESIS, no demostrada)
La semántica documentada de init: una definición posterior con `override` sustituye a una anterior; las RC de
`/vendor/etc/init` se procesan después de las de `overlay.d`/`init.rc`. Si es así, una definición en `overlay.d` sería
sustituida por la del fabricante (que ya trae `override`), y el RC del módulo (montado por Magisk en post-fs-data) llegaría
tarde porque init ya leyó los RC. Falta la prueba: el script extrae del log del kernel las líneas `Parsing file …`.
No se descarta aún una alternativa válida; no se implementa nada hasta ver el orden real.
`wrap.*` aplica a procesos de app lanzados por zygote, no a servicios nativos de init (consistente con tu advertencia 4).

### 2.4 Discrepancia de identidad
`research/MOTO_G85_INIT_BOOT_PAL_AUDIT_2026-11-04.md` registra (corrección del 2026-11-05) Android 14 / SDK 34;
la orden indica Android 15 / SDK 35. El script reporta `ro.build.version.*`. Las conclusiones sobre orden de init dependen de esto.

### 2.5 PR #24 / #25 / #35 (REST; GraphQL no disponible en esta sesión)
Los tres figuran **fusionados en `main`**. Eso no basta; contrasté con el código:

| Punto reportado en #25 | Estado en el código de `main` |
|---|---|
| `CLOCK_MONOTONIC` leído después de `query_()` | Corregido (`ivanna_qualcomm_pal_clock_integration.hpp`: lee el reloj tras `query_`) |
| Orden del reloj respecto al seqlock en `PalAsrcBridge` | Corregido (`updateDrift`: `nowNs()` tras el snapshot) |
| Servo/estado al reconfigurar | Corregido en `configure()` (`clock_.reset()`, ratio 1.0) |
| Snapshot inexistente vs inaccesible | Corregido en `probe_hal_clock_source.sh` (comprueba `-x` del directorio) |
| Compatibilidad `clock_snapshot` | Presente (`kLegacyHardwareClockSnapshotPath`); falta verificar que el lector la consuma |

Comentarios de revisión de #24 (CodeRabbit): existen 14 (arbitraje AUX/WFS, historial en bypass, `assert` con `NDEBUG`, etc.).
**No los he verificado uno a uno todavía**; no se marcan resueltos.
Además `origin/main` avanzó a `018e330` (v2.5.358) con *forced update* respecto a mi clon (`003aefd`): hay que reconciliar antes de cualquier push.

## 3. Archivos creados (sin commit)
- `tools/selinux/sepolicy_query.py` — consultas sobre la política binaria, autovalidado.
- `hal_integration/moto_g85/pal_preload/collect_hal_preload_phase1.sh` — evidencia de solo lectura (sintaxis verificada con `sh -n`).
- este informe.

## 4. Pendiente / riesgos
- Ejecutar el script en el dispositivo y adjuntar `/data/local/tmp/ivanna_phase1.txt` (orden RC, `AT_SECURE`, etiqueta de la .so, Android 14 vs 15).
- Hasta entonces no hay diseño de inyección validado. No se reinició ni se tocó el HAL.
- Fase 2 (implementación) requiere tu confirmación explícita antes de: aplicar la regla `noatsecure` (implica reiniciar el HAL o el dispositivo) o tocar `/vendor`.
