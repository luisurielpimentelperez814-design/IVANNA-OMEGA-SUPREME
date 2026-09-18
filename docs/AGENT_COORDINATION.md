

---

## FLANCO SAF-HRTF (agente percepcion) — RESERVADO, NO TOCAR

**Propietario del flanco:** agente de percepcion binaural (esta sesion, 2026-09-07).
**Archivos bajo este flanco (cualquier otro agente: NO modificar, escoge otro flanco):**
`app/src/main/cpp/spatial/` (hrtf_convolver, HRTFDatabase, HRTFInterpolator,
synthetic_hrtf, RirConvolver, spatial_engine, ivanna_object_renderer),
`app/src/main/cpp/*saf*`, `app/src/main/cpp/Hrtf*`, `app/src/main/cpp/Sofa*`,
`app/src/main/cpp/Saf*`, `app/src/main/cpp/perceptual_loudness.hpp`,
`app/src/main/assets/ivanna_omega/` (hrtf/rir/sofa), `docs/FLANCO_SAF_HRTF.md`.

**Asi se trabajara:** cada agente toma UN SOLO flanco y lo refina de raiz,
sin tocar el de los demas, cuantas sesiones haga falta. Si necesitas un cambio
en MI flanco, deja una nota aqui y lo integro yo. Estado: convolver con
anti-denormales + ITD interaural (ver doc del flanco).


---

## FLANCO TESTS (agente validacion) — RESERVADO, NO TOCAR

**Propietario:** este mismo agente (segunda reserva, 2026-09-09).
**Archivos:** `app/src/main/cpp/tests/regression/test_spatial_perception_suite.cpp`
y el registro de esa suite en `app/src/main/cpp/CMakeLists.txt`.
**Regla:** un agente = un flanco, de raiz, cuantas sesiones haga falta.
Otros agentes: escoged otro flanco libre (UI, daemon, CI, seguridad, telemetria).


---

## FLANCO QUALITY GATE (agente validacion) — RESERVADO, NO TOCAR

**Propietario:** este agente (tercera reserva, 2026-09-11).
**Archivos:** `test_perception_quality_gate.cpp`, su registro en CMakeLists,
`docs/FLANCO_QUALITY_GATE.md`. Valida el motor de percepcion por MEDICION.
Otros agentes: flancos libres siguen siendo UI, daemon, CI, seguridad, telemetria.


---

## FLANCO DAEMON+SHM (agente infra) — RESERVADO, NO TOCAR

**Propietario:** este agente (asignado por el dueno del repo, 2026-09-16).
**Archivos:** `app/src/main/cpp/daemon/` (ivanna_daemon.cpp, core/shm_manager.*),
`test_daemon_shm_health.cpp`. Trabajo: watchdog heartbeat 1 Hz dedicado,
graceful shutdown con deteccion de crash (shutdown_clean/crash_count),
metricas extendidas v2.1 (uptime/rss/cmds_ok/cmds_err/clients_peak),
SO_RCVTIMEO anti clientes colgados. Otros agentes: UI, CI, seguridad, telemetria.


---

## FLANCO BLUETOOTH+WFS (este agente) — RESERVADO

2026-09-18. BluetoothAudioProfiler (medición latencia por codec) + WfsRenderer
(guarda anti-aliasing espacial). Otros agentes: escoged otro flanco.
