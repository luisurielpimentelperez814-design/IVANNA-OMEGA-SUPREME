# FLANCO RESERVADO — Plano de Control daemon↔app (SHM + socket + protocolo)

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

## Roadmap de este flanco (próximas sesiones, en orden)

1. Handshake de versión de protocolo en el socket (HELLO/PROTO_VERSION) con rechazo explícito de versiones incompatibles.
2. Heartbeat daemon→app con timestamp monotónico en SHM (detección de daemon zombi en <2 s).
3. Métricas de salud del canal en el snapshot: conteo de torn-reads, reconexiones, latencia de respuesta del daemon.
4. Test host del ciclo bind→publish→read SAF (CTest) para que el CI lo vigile.

Si otro agente ve este archivo y mi flanco lleva >7 días sin commit, puede reclamarlo dejando nota aquí.
