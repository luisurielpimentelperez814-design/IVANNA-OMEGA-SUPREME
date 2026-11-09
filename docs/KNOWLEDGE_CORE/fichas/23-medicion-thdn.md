# Ficha 23 — Medición: THD+N, IMD, SNR

**Dominio:** Medición y validación · **Estado:** en uso (tests host)
**Fuentes:** `VEREDICTO.md` §3#4–#5; `FUSION.md`; `test_measure_baseline.cpp`.

## Concepto

Para saber si algo suena mejor hay que medirlo. Las tres medidas básicas:
**THD+N** (distorsión armónica total + ruido: cuánto de lo que sale NO es la
señal), **IMD** (distorsión de intermodulación: dos tonos crean tonos
fantasma), **SNR** (señal vs ruido de fondo). Se miden en dB: más negativo
= más limpio. Pero ojo: THD+N global NO predice envolvencia ni calidad
percibida (FRONTERA §1.4). Un THD de −140 dB no dice nada de si el sonido
envuelve. Las medidas objetivas sirven para iterar rápido; la verdad la
decide el ABX (ficha 12).

## Definición y valores

- **THD+N:** `20·log10(√(Σ armónicos² + ruido²) / fundamental)`. Medido con
  seno a −3 dBFS o −1 dBFS, a 100 Hz / 1 kHz / 5 kHz.
- **IMD SMPTE:** dos tonos (60 Hz + 7 kHz, 4:1); mide los productos de
  intermodulación alrededor de 7 kHz.
- **SNR:** `20·log10(señal / ruido)` con la señal a nivel de referencia.
- **Regla:** medir SIEMPRE a nivel declarado (−3 dBFS o −1 dBFS) y a
  frecuencia declarada; un THD sin nivel ni frecuencia no significa nada.

## Mediciones de IVANNA (host, cadena núcleo)

| Medición | Valor |
|---|---|
| THD+N @1 kHz, −3 dBFS (cadena núcleo) | **−76.67 dB** |
| THD+N @100 Hz, −3 dBFS | **−106.40 dB** |
| THD+N @1 kHz, −1 dBFS | −76.69 dB |
| IMD SMPTE | **−130.70 dB** |
| Throughput host (cadena núcleo) | 136.7× RT |
| Latencia algorítmica | 0 muestras (bloque 128 = 2.67 ms) |
| ORBE THD+N @1 kHz | **−139.33 dB** |
| HOLOS retuneado THD+N @1 kHz | −94.0 dB |
| HOLOS retuneado IMD | −159.5 dB |
| Compresor (2ª etapa) | −73 dB @150 Hz, −93 @500, −105 @1k, −129 @5k |
| EvolutionaryEQ THD+N | ~−120 dB |

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| `test_measure_baseline.cpp` | `app/src/main/cpp/tests/` | Cadena núcleo host: THD+N, IMD, latencia, throughput. |
| Tests por módulo | `app/src/main/cpp/tests/` | Cada módulo tiene su test de THD/estabilidad. |

## Preguntas abiertas (para la evolución)

1. ¿Medición en DISPOSITIVO (no solo host x86) de THD+N/IMD/latencia/CPU?
   El repo no la tiene; hay que crearla, no inventarla.
2. ¿THD por armónico (no solo total) para validar el kernel Volterra
   (ficha 02: predicción <1 dB por armónico)?
3. ¿Medidas perceptuales (PEAQ, POLQA) como complemento del ABX?
4. ¿Calibración del micrófono del G85 (respuesta medida vs referencia)
   para que el sensor (ficha 04) sea cuantitativo?

## Hueco 5 cerrado — Latencia extremo a extremo (2026-11-09)

**Estado: MEDIDO PARCIALMENTE (datos en vivo).** AudioFlinger salida 21.00 ms
(mixPeriod 4.00 ms); app IVANNA reporta 173.9 ms (±0.1 ms) de latencia total;
presupuesto estimado app→bocina ~30–40 ms. La medición definitiva es
`RoomMeasurement.analyze().latencyMs` (retardo acústico salida→mic, todos los
tramos en una cifra) — protocolo listo, falta ejecutarla. Detalle en
`docs/KNOWLEDGE_CORE/GAPS_FILLED.md` §5.
