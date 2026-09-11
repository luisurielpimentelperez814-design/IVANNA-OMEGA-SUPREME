# FLANCO RESERVADO — Plano de Control daemon↔app (SHM + socket + protocolo)

> 🔗 **Coordinación consolidada:** el índice maestro de todos los flancos
> es `AGENT_CLAIMS.md` en la raíz — revísalo también antes de reclamar o
> tocar cualquier área. Este archivo satélite se preserva por su detalle,
> pero puede estar desactualizado si no se edita en ambos lugares.


**Propietario de flanco:** agente-control-plane (esta línea de trabajo).
**Fecha de reserva:** 2026-09-07 · **Estado:** ACTIVO — trabajo en curso.
**Mensaje a otros agentes:** NO toques los archivos listados abajo. Si tu flanco
necesita cambiarlos, coordina primero: abre una nota en este mismo directorio
(`docs/FLANCO_*.md`) describiendo el conflicto y trabaja en otro frente mientras.
Cada agente toma UN solo flanco y lo lleva a magistral. Este es el mío.

## Alcance EXACTO de este flanco (solo yo modifico)

- `app/src/main/cpp/daemon/**` (ivanna_daemon.cpp, control/command_server.*, core/shm_manager.*, core/omega_control_bus.*)
- `app/src/main/cpp/shm_hyperplane.cpp`
- `app/src/main/cpp/include/omega_shared.h`, `omega_control_bus.h`
- `app/src/main/java/com/ivanna/omega/magisk/**` (OmegaEngineBridge.kt, ShmManager.kt, MagiskBridge, OmegaDaemon.kt)
- `magisk_module/service.sh`, `magisk_module/core/ivanna_autonomous_core.sh` (solo la sección DAEMON/socket), `magisk_module/sepolicy.rule`

## Fuera de mi flanco (NO tocaré — pertenecen a otros)

- DSP puro (`dsp/`, `spatial/`, `neuromorphic/`, `IvannaFusionCore*`) → flanco DSP
- UI Compose (`ui/**`) → flanco UI
- Assets/datasets HRTF-RIR-SOFA (`assets/`, `hrtf/`, `rir/`) → flanco datasets
- Asistente/IA (`ai/`, `assistant/`) → flanco agente

## Invariantes que este flanco garantiza (no romper jamás)

1. `ShmHeader` ABI = 32 bytes (commit 3d3d1dc1). Cualquier cambio de tamaño exige bump de `SHM_ABI_VERSION` y actualización coordinada de `ShmManager.kt`.
2. Los DOS buses SHM coexisten: **canal A** `OmegaShmManager` (backing file `/data/adb/ivanna_omega/omega_shm`, SAF frame en base+sizeof(ShmHeader)) y **canal B** `OmegaControlBus`/`omega_control_snapshot` (daemon↔omega_effect). No fusionarlos.
3. Socket: primario abstracto `@omega_daemon_socket`, fallback filesystem `/data/adb/ivanna_omega/omega_daemon.sock`, fallback TCP `127.0.0.1:12121`. El orden de sonda en el bridge es ese.
4. Handshake SHM real por SCM_RIGHTS (Modo B, commit 78aed525) — no regresar a creación local de región.
5. Todo ByteBuffer SHM se lee con `ByteOrder.LITTLE_ENDIAN` explícito.
6. El daemon NUNCA aborta sin dejar un socket alcanzable (fallback fs implementado).

## Estado del flanco al reservar

- Socket: triple vía (abstracto/fs/TCP) + keepalive 5 s + sepolicy hasta untrusted_app_35. Verificación de bind real en el executor (panel ya no puede mostrar CORRIENDO+DESCONECTADO sin causa logueada).
- SHM: layout fijado por static_assert (32 B), endianness LE en reader, productor SAF escribe frame 16 B en canal A con seqlock que el reader valida.

## Roadmap de este flanco — COMPLETADO 2026-09-10

1. ✅ Handshake HELLO/PROTO_VERSION en el socket (fc5061f) — OMEGA_PROTO_VERSION=1, respuesta {proto, shm_version, ctrl_version, compatible}; aditivo, clientes viejos intactos.
2. ✅ Heartbeat daemon→app en SHM (ac741f1) + fix de dominio de reloj (32ae63c: uptimeMillis/CLOCK_MONOTONIC en vez de elapsedRealtime — antes falso "daemon zombi" tras cualquier deep sleep).
3. ✅ Métricas de salud del canal (e8db0f4..2ee6210) — bloque SHM_HEALTH_OFFSET (+24..47): SAF publicados, heartbeats, writes rechazados, comandos (JSON+texto), clientes; API Kotlin channelHealth() (01da7c7).
4. ✅ Test host del ciclo SHM completo (c9cb92a, 38/38 checks) — layout/ABI/seqlock/overflow/restart/validación/salud, integrado en la puerta CTest del CI.

Además: validador canónico validateShmHeader en C++ (97154f2) y su espejo Kotlin (0c59988) — ningún reader acepta ya un mmap sin magic/version/state_size; asserts lock-free compile-time (21614b5); frame_len solo del canal de datos (79ec67e); SHM_HEARTBEAT_OFF definido (4130d33, compileKotlin roto en ac741f1); sepolicy v3.1 — la app ya puede abrir omega_shm por ruta directa (fd8c854); fix build daemon standalone (2d5958f).

**Estado: ENTREGADO 2026-09-10.** Flanco libre para mantenimiento. Si lo retomas, respeta los 6 invariantes de arriba y actualiza esta nota.
