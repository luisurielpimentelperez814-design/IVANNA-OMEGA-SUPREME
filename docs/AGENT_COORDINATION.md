

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
