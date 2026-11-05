# Auditoría del init_boot — Motorola Moto G85 5G

**Fecha de análisis:** 2026-11-05  
**Artefacto analizado:** `ivanna_init_boot_a.img`  
**SHA-256:** `db3dd9987183fb843b7b274f43dd7ec66a7670d737853df6d00f24361759e9e1`  
**Tamaño:** 8,388,608 bytes (8 MiB)

> **Conclusión ejecutiva:** esta imagen contiene el ramdisk de arranque de una compilación Motorola Android 14 / SDK 34. No contiene PAL, AGM, Audio HAL ni un servicio de IVANNA. Su contenido no proporciona el puente de reloj PAL→ASRC y no justifica modificar `init_boot`. La imagen tampoco demuestra ser el `init_boot` actualmente activo en el teléfono Android 15.

## 1. Identificación de la imagen

- Formato: Android boot image, cabecera v4.
- Tamaño de kernel declarado en cabecera: 0 bytes (esperado para una imagen `init_boot`).
- Tamaño declarado del ramdisk: 1,796,616 bytes.
- Tamaño de cabecera: 1,584 bytes.
- Dispositivo: Motorola Moto G85 5G, `malmo` / `malmo_g`.
- Arquitectura de los ejecutables inspeccionados: ELF64 AArch64.
- Android identificado por las propiedades de la propia imagen: Android 14, SDK 34.
- Build ID: `UUOS34HV-V1-ST11.1`.
- Fingerprint: `motorola/malmo_g/malmo:14/UUOS34HV-V1-ST11.1/132c31:user/release-keys`.
- Fecha de build: 2025-09-29 05:03:19 CDT.

El fingerprint pertenece al boot image examinado. No debe sustituir ni confundirse con el fingerprint del sistema actualmente instalado.

## 2. Inventario recuperado del ramdisk

| Ruta | Tamaño | Observación |
|---|---:|---|
| `.backup/.magisk` | 143 B | Estado/configuración de Magisk |
| `.backup/.rmlist` | 76 B | Lista de rutas de overlay |
| `.backup/init.xz` | 892,948 B | Copia comprimida de init |
| `init` | 184,376 B | Ejecutable init |
| `overlay.d/sbin/init-ld.xz` | 1,536 B | Componente comprimido de overlay |
| `overlay.d/sbin/magisk.xz` | 146,424 B | Componente comprimido de Magisk |
| `system/bin/snapuserd_ramdisk` | 1,168,248 B | Ejecutable snapuserd |
| `system/etc/init/snapuserd.rc` | 537 B | Servicios y sockets snapuserd |
| `system/etc/ramdisk/build.prop` | 828 B | Propiedades del boot image |

### 2.1 Estado Magisk recuperado

El archivo `.backup/.magisk` contiene:

- `KEEPVERITY=true`
- `KEEPFORCEENCRYPT=true`
- `RECOVERYMODE=false`
- `VENDORBOOT=false`
- `PREINITDEVICE=metadata`
- `SHA1=7f2c01d4d6be912fb397c06f3c5834fa822db9f4`

Estos valores describen el estado guardado en esta imagen. No acreditan por sí solos la configuración del módulo Magisk instalado actualmente.

### 2.2 Servicios snapuserd

El archivo `system/etc/init/snapuserd.rc` declara:

- Servicio `snapuserd /system/bin/snapuserd`.
- Servicio `snapuserd_proxy /system/bin/snapuserd -socket-handoff`.
- Ambos servicios son `disabled` y `oneshot`, con usuario `root`, grupos `root system` y dominio SELinux `u:r:snapuserd:s0`.
- Socket `snapuserd`: tipo `stream`, modo `0660`, propietario/grupo `system:system`.
- Socket `snapuserd_proxy`: tipo `seqpacket`, modo `0660`, propietario/grupo `system:root`.
- Cuando `snapuserd` se detiene, se establece `snapuserd.ready=false`.

El ejecutable contiene referencias a `io_uring`, `eventfd`, `mmap`, `socket_handoff`, `socket_proxy`, `ReadAheadAsyncIO` y colas de operaciones. Corresponden a mecanismos de snapshots/almacenamiento y Virtual A/B. No son una interfaz de audio ni una implementación de ASRC.

## 3. Hallazgos negativos y límites de alcance

En los archivos extraídos de este `init_boot` no se identificó una implementación de:

- Qualcomm PAL o `libar-pal.so`.
- `libpalclient.so` o AGM/`libagm.so`.
- `audio.primary.*` ni otro HAL de audio.
- AudioFlinger, `audioserver`, PCM o una ruta de procesamiento de audio.
- Servicio, socket o configuración de IVANNA para un puente de reloj de audio.

El hallazgo negativo se limita a esta imagen y a su ramdisk extraído. No demuestra que esos componentes no existan en las particiones `vendor`, `odm`, `system` o en el firmware activo.

## 4. Relación con el puente PAL→reloj→ASRC de IVANNA

El problema de integración debe resolverse en el límite del stream de salida que realmente posee el handle PAL y el convertidor activo. El ramdisk analizado no proporciona ese handle, esa API ni un actuador ASRC.

En una captura de firmware distinta y anterior se habían observado estos símbolos en `/vendor/lib64/libar-pal.so`:

- `pal_get_timestamp`, offset ELF reportado `0x2ec120`.
- `pal_stream_get_mmap_position`, offset ELF reportado `0x2ec180`.

Son observaciones de una biblioteca concreta, no direcciones universales. No se deben codificar como offsets fijos ni atribuir a la imagen Android 14 aquí analizada. La integración debe resolver símbolos dinámicamente y validar build-id, ABI, prototipos y disponibilidad en el proceso objetivo.

La arquitectura documentada para el puente mantiene separados:

1. **Plano de control:** descubrimiento de ruta, apertura/cierre del stream, permisos, generación de ruta y diagnóstico.
2. **Plano de tiempo real:** lectura de una muestra ya publicada y parámetros precomputados, sin PAL, Binder, sockets, I/O, asignación dinámica ni esperas de mutex dentro del callback.

Una muestra de reloj válida debe asociar de forma coherente posición de frames y timestamp monotónico, e identificar sample rate, stream/sesión, ruta/generación, secuencia y flags de discontinuidad. No se deben combinar lecturas independientes como si fueran una única instantánea.

La corrección de deriva requiere además un convertidor de razón variable ya presente en el grafo de reproducción y una API comprobada para escribir su ratio, con unidades y confirmación de aceptación. Un selector de fuente de reloj, un payload de timestamp o un cambio de formato multimedia no equivalen a un actuador ASRC.

## 5. Decisión de ingeniería

- **No modificar este `init_boot` para resolver PAL/ASRC.**
- **No reutilizar `snapuserd` ni sus sockets** como transporte de reloj de audio.
- No inyectar PAL desde el ramdisk ni asumir que un daemon system puede cargar bibliotecas vendor fuera de su linker namespace/política SELinux.
- Mantener el adaptador PAL vinculado al stream real que posee el handle; el productor publica snapshots acotados y el consumidor RT no bloquea.
- Conservar la corrección hardware deshabilitada hasta demostrar tanto la muestra frames+timestamp como el actuador de razón variable real.

El puente debe integrarse en el HAL/proceso con acceso legítimo al stream, o mediante un IPC mínimo desde ese componente autorizado. El arranque temprano de Magisk no implica que PAL o el grafo de audio estén disponibles.

## 6. Validación pendiente en el firmware Android 15 activo

Antes de activar cualquier corrección en el Moto G85:

1. Registrar el fingerprint actual del sistema y el build-id/hash de `audio.primary.blair.so`, `libar-pal.so`, `libpalclient.so` y `libagm.so`.
2. Verificar símbolos y prototipos PAL contra esas bibliotecas exactas; no reutilizar offsets de otro build.
3. Durante reproducción real, capturar posición de frames y timestamp desde una misma consulta/snapshot coherente.
4. Identificar el convertidor ASRC activo en la ruta y demostrar su contrato de control, unidades de ratio y aceptación/lectura posterior.
5. Medir monotonía, deriva, jitter, discontinuidades, underruns y cambios de ruta.
6. Probar pause/resume, standby, reapertura, cambio de sample rate, desconexión USB y reinicio del daemon.
7. Confirmar que ningún callback de audio espera por IPC, Binder, filesystem, PAL o locks bloqueantes.
8. Mantener fallback nominal y rollback sin tocar controles de mixer no identificados.

La sonda de solo lectura está en `hal_integration/tools/probe_moto_g85_pal.sh`. Su salida es evidencia de descubrimiento; no demuestra por sí sola que un actuador ASRC esté conectado.

## 7. Reproducibilidad y procedencia

El SHA-256 del artefacto de entrada es:

```
db3dd9987183fb843b7b274f43dd7ec66a7670d737853df6d00f24361759e9e1  ivanna_init_boot_a.img
```

El archivo binario completo y los ejecutables extraídos no se incorporan al repositorio: son artefactos de firmware, no código fuente necesario para construir IVANNA. Se conserva aquí el inventario, los metadatos, el hash y las conclusiones reproducibles. La transcripción integral y el paquete de extracción permanecen como artefactos de auditoría externos.

## 8. Documentación relacionada

- [Integración de reloj Qualcomm PAL para Moto G85](../../hal_integration/MOTO_G85_PAL_CLOCK_INTEGRATION.md)
- [Guía de integración HAL](../../hal_integration/README.md)
- [Análisis profundo de ASRC para Moto G85](../ASRC_MOTO_G85_IMPLEMENTATION_DEEP_DIVE.md)

---

**Estado:** auditoría documental del `init_boot` completada. No es una prueba de integración PAL/ASRC en hardware. No se modificó ninguna partición del teléfono.
