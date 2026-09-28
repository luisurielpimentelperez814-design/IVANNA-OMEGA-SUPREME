# ✅ FLANCO CERRADO Y ENTREGADO: PhaseOracle — motor Kalman de fase
**Entregado por:** sesión Genspark, 2026-09-08. **Regla de coordinación:** UN
flanco por agente; cualquier otro agente: **NO toques estos archivos salvo
para extender el núcleo con su propia suite host** — escoge otro flanco.

## Estado final (todo verificado en corrida real, no supuesto)
1. `phase_oracle_kalman.hpp` (NUEVO) — núcleo `PhaseKalman3`, fuente única
   de verdad, C++17 sin JNI. Corrige de raíz: P=F·P·Fᵀ+Q completo por
   columnas, update (I−KH)P con fila 0 vieja, simetrización, guardas NaN/Inf,
   defaults recalibrados con repro medido (seguimiento seno 440 Hz: 89%→21%
   del error de amplitud).
2. `tests/test_phase_oracle_kalman.cpp` (NUEVO) — 7 tests GTest de
   propiedades reales (seguimiento, covarianza PSD, 1M muestras sin NaN,
   cue de transitorio, ganancia no colapsada, look-ahead, reset). Suite host
   completa: 67/67 PASS.
3. `phase_oracle.cpp` — migrado al núcleo; funciones decorativas eliminadas
   (verificado cero refs externas); ABI JNI intacto; `nativeSetPhaseParameters`
   sigue exponiendo Q al runtime.
4. `phase_oracle_bridge.hpp` — `transient_cue()` con escala recalibrada
   (1/40, medida: ataque 0→0.8 → cue ≈ 0.97; antes 1/5000 → 0.008, detector
   muerto). Firma C `phase_oracle_velocity()` intacta (consumidor DSP
   `biquad_envelope_bank.hpp` — flanco DSP — no requiere cambios).
5. `phase_oracle_refinements.hpp` — guard #ifndef real (el #endif era
   huérfano), duplicado BiquadEnvelopeBank eliminado, covarianza corregida.

## Pendiente para futura sesión (opcional, no bloquea)
- Evaluar si `predictSamples()` (look-ahead polinómico) debe alimentarse de
  la salida del filtro por muestra en vez de extrapolar del estado final del
  bloque (decisión de producto del flanco DSP).
- `nativeSetPhaseParameters` no valida entradas (Q negativa rompería la PSD):
  clamp en la capa Kotlin queda como mejora menor.
