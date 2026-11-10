# 5D FASE 0 — Campo acústico-neural Ψ(x, y, z, t; η) en host

**Fuente:** `~/workspace/analysis-ivanna/PROPUESTA_5D.md` (§2–§3, §5).
**Alcance:** solo Fase 0. Host-only, sin hardware, sin cableado al DSP real,
sin tocar el hilo RT, el efecto omega ni las Rutas A/B. Sin commit/push
asociado a esta línea (el archivo vive en el árbol de trabajo).

## Qué es 5D aquí (y qué no)

La 5ª coordenada es el **estado neural del oyente**, formalizado como el
escalar **η(t) ∈ [0, 1]**: la coherencia neuro-acústica, es decir, la
correlación (ventana 2–4 s, banda 1–8 Hz) entre la envolvente acústica y la
respuesta cortical medida por TRF. El sistema completo vive en
**R³ × R × N** (espacio × tiempo × estado neural); la forma mínima 5D es
`(x, y, z, t, η)`.

No es una dimensión espacial nueva (Kaluza-Klein es real pero inaccesible al
sonido por ~10⁻⁷⁰), ni un segundo tiempo (gauge-redundante), ni la
frecuencia (que es el andamio de representación por banda, no la dimensión).
Ver PROPUESTA_5D.md §1 para los veredictos y §7 para los límites honestos.

## Qué implementa la Fase 0

Módulo `spatial/BioControl5D.hpp` (header-only, doubles, sin allocations en
`update()`, determinista, sin dependencias Android):

| Pieza | Contenido |
|---|---|
| `FiveDState` | η, η₀ (basal, default 0.5), τ_η (default 2.0 s), η* vigente, flags `sensorPresent` / `fading` |
| `BioControl5D::update(dt, etaStar)` | (3.2) `dη/dt = (η*(u) − η)/τ_η`, RK2 punto medio, η clampada a [0, 1], dt clampado a τ_η (sin sobreoscilación) |
| `gammaB(gamma0, alpha, dt)` | (3.3a) Γ_b con clamp \|α·Δη\| ≤ 0.35 y ±3 dB, slew 0.5 dB/s (en dominio dB) |
| `orbeRadius(r0, beta, dt)` | (3.3b) r_ORBE con clamp ±0.2 m, slew 0.05 m/s |
| `kuramotoK(k0, gamma, dt)` | (3.3c) K con clamp ±0.25 relativo, slew 0.5 rad/s² |
| `onSensorLost()` / `setSensorPresent()` | Congela η* y desvanece η → η₀ con paso exponencial exacto (τ_η); `isFiveDActive()` pasa a false de inmediato |
| `emergentToneAmplitude(A4, r, kappa=0.5)` | (3.7) amplitud del tono 884 Hz: `A4·r·(1 + κ·(η−η₀))` |
| `SyntheticEeg` | EEG sintético determinista (semilla fija): envolvente de referencia 0.15 Hz + amplitud ASSR 40 Hz modulada + ruido uniforme + artefacto de movimiento (−0.25 durante 0.5 s cada 7 s). Dos instancias con la misma semilla producen la misma η* bit a bit |

**Fail-closed (§3.4):** sin sensor, η = η₀ y todo vale defaults
(Γ_b = Γ_b⁰, r_ORBE = r₀, K = K₀, tono = A4·r): el 5D degenera en HOLOS 4D.

**El lazo nunca sube el nivel master ni toca el limiter:** por diseño la API
no tiene ninguna función de nivel. `test_5d_phase0.cpp`
(`NoMasterLevelControlExists`) lo verifica en tiempo de compilación con
detección SFINAE (`setMasterLevel` / `setMasterGain` / `setLimiterThreshold` /
`setLevel` no existen); si alguien añade una, el test deja de compilar.

**Primera llamada de cada actuador:** snap al objetivo (no hay valor previo
del que limitar la velocidad); a partir de la segunda, el slew-rate manda.
Es el comportamiento documentado en el header.

## Ecuaciones

```
(3.2)  dη/dt = (η*(u) − η) / τ_η,  τ_η ≈ 1–4 s
(3.3a) Γ_b(t)    = Γ_b⁰·(1 + α_b·(η−η₀)),  |α_b·(η−η₀)| ≤ 0.35 (±3 dB)
(3.3b) r_ORBE(t) = r₀ + β·(η−η₀),          |β·(η−η₀)| ≤ 0.2 m
(3.3c) K(t)      = K₀·(1 + γ·(η−η₀)),      |γ·(η−η₀)| ≤ 0.25
       |dΓ_b/dt| ≤ 0.5 dB/s, |dr_ORBE/dt| ≤ 0.05 m/s, |dK/dt| ≤ 0.5 rad/s²
(3.7)  e₅[n] = A4·r(t)·(1 + κ·(η−η₀))·sin(2ψ(t)),  κ ≈ 0.5
```

Base 4D heredada (sin cambios): d'Alembertiano □p = 0 + Kuramoto
(K = 40 rad/s, r = 0.9636, tono emergente 884 Hz en sin(2ψ)).

## Cómo correr los tests

```sh
cd ~/workspace/ivanna/build-tests
~/.local/bin/cmake ../app/src/main/cpp/tests   # el source de build-tests es
                                                # app/src/main/cpp/tests, no la raíz
~/.local/bin/cmake --build . -j2 --target test_5d_phase0
./test_5d_phase0
~/.local/bin/ctest   # suite completa
```

`test_5d_phase0.cpp` añade **15 tests** (suite `FiveDPhase0`): convergencia
de η con constante τ_η y sin sobreoscilación; dirección y clamp ±3 dB de Γ_b;
clamp ±0.2 m y slew de r_ORBE; clamp ±0.25 y slew de K; fail-closed sin
sensor; desvanecimiento tras perder el sensor; correlación amplitud–η del
tono 884 Hz (r > 0.999 en barrido); ausencia de control de nivel por diseño;
determinismo del EEG sintético; lazo completo con EEG sintético (30 s);
robustez (dt ≤ 0, τ_η fuera de rango).

## Qué queda para Fases 1–3 (sensor real)

- **Fase 1 — Medir el observable neural.** Decisión de sensor (recomendado:
  MW75 Neuro, EEG crudo 500 Hz, ~$500, envío a México sin verificar). Prueba
  de viabilidad en el Moto G85 con música sonando: (i) ¿se ve la ASSR 40 Hz
  con tono modulado de calibración? (ii) ¿el envelope tracking con música
  real da ρ_env > 0.1 sostenido? (iii) ¿cuánto artefacto mete el movimiento
  de cabeza? **Criterio de muerte:** si (i) y (ii) fallan, el 5D es
  intesteable y se archiva como diseño sin medición (el sistema queda en
  4D + NPE modelo). No se compra más hardware ni se sigue.
- **Fase 2 — Cerrar el lazo en un parámetro.** Solo Γ_b driven por η real
  (medida por el BioObserver, que aún no existe: estimador de ρ_env, ASSR,
  P_α y F a partir del EEG). Medir P1: Δρ_env ≥ +0.10 sostenido (60 s) y P2:
  envolvencia SAQI +15 puntos con ABX ≥12/16.
- **Fase 3 — 5D completo.** Todos los actuadores (Γ_b, r_ORBE, K) driven por
  n(t); medir C(f) en 1–8 Hz (P4: pico ≥ 1.5× basal) y el tono 884 Hz
  modulado por η (P5: correlación amplitud–η r > 0.6). Puerta auditiva
  completa (§6 de la propuesta). Nada se enciende por defecto sin GREEN
  replicado en dos sesiones.

Sin sensor no hay 5D: la 5ª coordenada solo existe si hay un sensor que la
mida. Todo el procesado EEG es local en el teléfono; el perfil
`bio_profile.json` no sale del dispositivo; no es medicina (EEG aquí es
instrumentación de audio, no diagnóstico).
