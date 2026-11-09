# Ficha 04 — Phi (Hiper-Operador de Estado)

**Dominio:** B (matemáticas/DSP) · **Estado:** en producción, activo por defecto
**Fuentes:** `docs/HYPER_STATE_OPERATOR.md`; `docs/HOMOGENEOUS_FUSION.md`.

## Concepto

Phi es la evolución de HOLOS al dominio temporal: UN solo operador de estado
continuo, muestra a muestra, **SIN FFT por bloques y SIN STFT**, que fusiona
tres vectores de estado en una sola ganancia unificada. Es la Fusión
Homogénea: bucle cerrado neural + campos de onda + pre-distorsión Volterra +
sensado Back-EMF + Bitstream Zero-PCM, en un solo operador matricial
neuro-electromecánico de estado continuo.

## Definición y fórmulas

```
Y(t) = x(t) + wet · [ Φ(s_neuro, s_emf, s_field) · x(t) − x(t) ]
```

- `s_neuro(t)` — NPE on-device (modelo coclear + LIF) + interfaz EEG real
  (frames `NeuroFrame`: kind 2=EEG, 3=ABR, 4=fNIRS; response/coherence[8]
  en bandas Greenwood; quality). Sin frames, el NPE es el estado neuro vivo.
  Gobierna modulación neuro (ancho M/S ±10%, micro-fase, atenuación ≤2%).
- `s_emf(t)` — SOLO medición real: V/I reales (observador `EmfEstimator`
  Thiele-Small) o back-EMF real. Sin fuente real, el término EMF es
  **identidad**: ningún modelo sustituye a la medición. En Android no hay vía
  eléctrica (Aux 3.5 mm es solo salida): el altavoz se observa
  acústicamente por micrófono, no eléctricamente.
- `s_field(t)` — campo medido por micrófono (`RealAcousticSensor`):
  `SensorFrame` con 24 bandas ERB (magnitude_db, error_db, coherence,
  level_dbfs). `error_db` gobierna la corrección Γ; `coherence` es el peso
  de confianza por banda.
- `amount=0` es **identidad bit-exacta**. Una sola ganancia unificada, una
  sola guarda de pico. Fail-closed. **No hay física nueva.**

## Valores clave

| Parámetro | Valor |
|---|---|
| Dominio | Muestra a muestra, sin FFT, 24 bandas ERB |
| Modos | MEDIDO (con SensorFrame) / APROXIMADO (sin sensor, fail-closed) |
| Amount default | 0.5 (activo por directiva de Esse) |
| Target | Harman 2018 (EliteTargetCurves) |
| Thiele-Small por defecto | Re 6 Ω, Le 0.5 mH, Bl 5 T·m (genéricos, no medidos) |
| Tests | Operador 20/20, curvas 8/8, sensor 10/10; suite 820/820 |
| Throughput host | 41–42× RT (33–34× con Volterra) |
| Sensor | Micrófono real, FFT 8192, 24 bandas ERB, 280× RT |

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| `HyperStateOperator.hpp` | `app/src/main/cpp/spatial/HyperStateOperator.hpp` | 882 líneas, header-only, muestra a muestra. |
| `EliteTargetCurves.hpp` | `app/src/main/cpp/spatial/EliteTargetCurves.hpp` | Harman 2018 default + élite-lean + difuso + sala. |
| Cableado Ruta A | `hyperProcessRouteA` tras HOLOS/ORBE, antes del limiter | Con guarda anti-doble-corrección. |
| Cableado Ruta B | `FusionCore` (`m_hyper`) | Corre en APROXIMADO (sin transporte de SensorFrame al daemon; ABI bump pendiente). |
| UI | Tarjeta HYPER · ESTADO Φ | CONTROL. |

## Mediciones que lo respaldan

- CTest 820/820 (100%); operador 20/20, curvas 8/8, sensor 10/10.
- Throughput host 41–42× RT. **Pendiente en dispositivo:** CPU/XRuns/
  temperatura, puerta auditiva ABX.

## Preguntas abiertas (para la evolución)

1. ¿Transporte de SensorFrame al daemon (ABI bump) para que Ruta B corra
   en MEDIDO en vez de APROXIMADO?
2. ¿Z_emf real en el G85 por vía acústica (micrófono) con qué precisión
   estima los parámetros Thiele-Small del altavoz?
3. ¿Phi + Volterra medido por banda (FRONTERA §2.2) en el mismo operador?
4. ¿El estado neuro (ASSR 40 Hz) modulando Γ en segundos, no por muestra?
