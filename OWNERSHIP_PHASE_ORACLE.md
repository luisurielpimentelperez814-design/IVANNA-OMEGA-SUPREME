# 🔒 FLANCO TOMADO: PhaseOracle — motor de predicción de fase (Kalman)

**Tomado por:** sesión Genspark (chat), iniciado 2026-09-07.
**Regla de trabajo:** UN flanco por agente. Este agente trabaja SOLO este
flanco, en ciclos cortos (un commit breve por cambio, push por ciclo).
Cualquier otro agente: **NO toques estos archivos** — escoge otro flanco
de AGENT_CLAIMS.md.

## Alcance exacto — no editar mientras esté aquí
- `app/src/main/cpp/phase_oracle.cpp`
- `app/src/main/cpp/phase_oracle_engine.hpp`
- `app/src/main/cpp/phase_oracle_bridge.hpp`
- `app/src/main/cpp/phase_oracle_refinements.hpp`
- `app/src/main/cpp/phase_oracle_kalman.hpp` (NUEVO — núcleo Kalman extraído,
  fuente única de verdad, compilable en host para tests)
- `app/src/main/cpp/tests/test_phase_oracle_kalman.cpp` (NUEVO — suite host)
- En `app/src/main/cpp/tests/CMakeLists.txt`: SOLO la línea que registra
  `test_phase_oracle_kalman` (el resto del archivo sigue siendo del flanco
  Tests host, que está CERRADO/entregado)

## Por qué este flanco y no otro
Es un módulo real, acotado y **sin dueño** (no aparece en los frentes
tomados de AGENT_CLAIMS.md: Daemon, DSP cadena, UI/UX, Conversación, IAEL,
Tests host). Tiene bugs numéricos verificables de raíz:

1. `phase_oracle.cpp::kalmanUpdate()` — la actualización de covarianza es
   **incorrecta**: usa `P[0][0]` ya actualizado para `P[1][0]`/`P[2][0]`
   (debe usar el valor viejo), y nunca actualiza `P[1][1]`, `P[2][2]` ni los
   términos cruzados → la covarianza queda asimétrica y el filtro no
   converge como Kalman real.
2. `phase_oracle.cpp::kalmanInit()` — velocidad inicial = 1000.0f → un
   transiente fantasma en el arranque (`transient_cue` ≈ 0.2 sin señal).
3. `phase_oracle.cpp` — funciones decorativas que NO hacen lo que dicen:
   `stockwellTransform()` es un `memcpy` (no es una transformación
   Stockwell) y `linearAutoencoder()` devuelve una constante (no es un
   autoencoder). Código muerto y engañoso → se elimina de raíz.
4. `phase_oracle_engine.hpp::KalmanPhasePredictor::predict_step()` —
   propagación de covarianza con fórmula errónea (`2*P_00*dt` espurio,
   `P_11*dt2` con dt²/2 en vez de dt²) y sin término cruzado P01.
5. `phase_oracle_refinements.hpp` — **`#endif` huérfano** al final (sin
   `#ifndef` que lo abra: si alguien incluye el header, error de
   compilación garantizado) y un `struct BiquadEnvelopeBank` DUPLICADO que
   colisiona con la clase real de `neuromorphic/biquad_envelope_bank.hpp`
   (prohibido por el criterio "sin dos implementaciones del mismo concepto").

**Explícitamente NO toca:** la cadena DSP (EQ/comp/exciter/widener/gain/
limiter), el daemon/Magisk/SHM, la UI Compose, Gemini/conversación, IAEL/
telemetría, ni el resto de tests/CMakeLists.txt. `phase_oracle_velocity()`
(consumida por `biquad_envelope_bank.hpp` del flanco DSP) mantiene su
firma C; solo mejora su matemática interna.

## Criterio de "terminado, world-class"
1. Núcleo Kalman correcto (predict F·P·Fᵀ+Q, update (I−KH)P con matriz
   3×3 simétrica completa) en un header puro C++17 sin JNI, compilable en
   host — fuente única de verdad.
2. Suite host GTest que VERIFICA propiedades reales: seguimiento de seno
   (error de predicción acotado), covarianza simétrica PSD, cero NaN en
   1M muestras, cue de transitorio que sube en ataques y decae, ganancia
   de Kalman convergente (ni congelada ni saltarina).
3. Suite completa 60+ tests host en verde (sin romper la puerta CI).
4. Código muerto/decorativo eliminado; headers con guard correcto.
