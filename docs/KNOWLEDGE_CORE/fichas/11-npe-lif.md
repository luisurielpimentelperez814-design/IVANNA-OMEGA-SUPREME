# Ficha 11 — NPE / LIF (motor neuromórfico)

**Dominio:** A (neuro-acústica) · **Estado:** en producción (modelo, no medida)
**Fuentes:** `BUCLE_CERRADO_NEURAL.md` §1–§2; `VEREDICTO.md` §2.1 paso 22.

## Concepto

El NPE (Neuromorphic Processing Engine) es un **modelo** del oído interno
que corre por muestra: la señal pasa por un banco coclear (envolventes por
banda, como las células ciliadas), un pool de neuronas LIF (Leaky
Integrate-and-Fire, como las neuronas auditivas), inhibición lateral,
compresión OHC y AGC. No mide neuronas reales: sus "spikes" los dispara la
envolvente de audio, no el cerebro. Es el actuador natural del bucle cerrado
neural: el modelo actúa rápido (por muestra); el sensor real (EEG/ASSR) lo
calibra despacio (segundos).

## Definición y valores

- **LIF:** `τ_m = 20 ms`, `V_rest = −70 mV`, umbral `−55 mV`, reset `−75 mV`,
  refractario `2 ms`, Euler explícito con `dt = 1/Fs`. Sin malloc, RT-safe.
- **Cadena NPE:** `x → envolventes (cues L,T,S,R) → LIF(spikes) → NHO(x) →
  inhibición lateral (spikes/32, cue S) → compresión OHC (cue L) → ensanche
  M/S por cue S → AGC lento (10 ms/300 ms) → master gain → rodilla suave`.
- **Apagado = bit-exacto**, con fade de 20 ms.
- **Banco coclear:** `BiquadEnvelopeBank` — envolventes por banda que
  alimentan el pool; es el modelo de células ciliadas.
- **OHC (células ciliadas externas):** amplificador activo de la cóclea
  (ganancia ~40 dB en silencio, compresiva). El modelo coclear del repo
  (`CochlearActiveInverseEngine`, 8 bandas Greenwood, Heun 10/80 ms) es la
  OHC sin el lazo eferente (ver ficha 26, MOC).

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| `NpeStereoCore` | `app/src/main/cpp/neuromorphic/NpeStereoCore.hpp` | Ruta B; equivalente JNI en Ruta A. |
| `LIFPool32` | `app/src/main/cpp/neuromorphic/lif_neuron_pool.hpp` | Pool de 32 neuronas LIF. |
| `BiquadEnvelopeBank` | `app/src/main/cpp/neuromorphic/biquad_envelope_bank.hpp` | Envolventes por banda. |
| Phi | `HyperStateOperator.hpp` | Usa los mismos bloques (envelope bank + LIF) con el mismo mapeo de 32 corrientes; NO reutiliza `NpeStereoCore` entero (su AGC/master gain de hasta 4× violaría "nunca subir el nivel"). |

## Mediciones que lo respaldan

- El NPE corre en ambas rutas; apagado es bit-exacto (testeado).
- **No hay validación contra fisiología real:** los spikes son del modelo,
  no del cerebro. La validación es por proxies (THD, escucha), no por EEG.
- Señales neurales reales candidatas (BUCLE_CERRADO_NEURAL.md): ASSR 40 Hz
  (nº 1, continua, responde a coherencia espacial), N1 ~100 ms, fNIRS
  (pico 6–8 s), bandas alfa/beta.

## Preguntas abiertas (para la evolución)

1. ¿El lazo MOC eferente (FRONTERA §2.4) dentro de la OHC mejora la
   detección en ruido y previene fatiga?
2. ¿ASSR 40 Hz como observador lento de la coherencia espacial de HOLOS/ORBE?
3. ¿Perfil individual (ASSR por banda ERB) como offset de Γ_b?
4. ¿El pool LIF con τ_m adaptativo por banda (como la cóclea real) mejora
   la detección de transitorios?
