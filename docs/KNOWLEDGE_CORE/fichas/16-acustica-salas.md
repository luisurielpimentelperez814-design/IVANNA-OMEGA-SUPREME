# Ficha 16 — Acústica de salas (RT60, EDT, C80, modos)

**Dominio:** Acústica de salas · **Estado:** en producción (RIR, 200 salas)
**Fuentes:** `VEREDICTO.md` §2.1 paso 19, §3#3; `FUSION.md` §1.3; Sabine; Schroeder.

## Concepto

Una sala es un filtro: el sonido directo llega primero, luego las reflexiones
tempranas (<80 ms, que dan intimidad y claridad) y luego la cola reverberante
(que da envolvencia). La sala se caracteriza por su tiempo de reverberación
(RT60: tiempo en que el nivel cae 60 dB), su claridad (C80: energía temprana
vs tardía) y sus modos (resonancias de la geometría). IVANNA convoluciona con
IRs de salas reales; la sala sintética se genera por BRIR.

## Definición y valores

- **RT60 (Sabine):** `RT60 = 0.161·V/A` (V = volumen m³, A = absorción total
  en sabins). Medido por T20: integral de Schroeder, −5…−25 dB extrapolado
  a −60 dB.
- **EDT (Early Decay Time):** caída de los primeros 10 dB ×6. Es lo que se
  PERCIBE como reverberación (más que el RT60).
- **C80 (claridad):** `10·log10(E_0-80ms / E_80ms-∞)`. C80 alto = claro
  (música de cámara +2…+6 dB); C80 bajo = envolvente (órgano −2…−5 dB).
- **Modos:** frecuencias de resonancia `f = (c/2)·√((nx/Lx)²+(ny/Ly)²+(nz/Lz)²)`.
  Por debajo de la frecuencia de Schroeder (~200 Hz en sala típica) dominan
  los modos discretos; por encima, el campo es difuso.
- **DRR (directo/reverberante):** ratio que codifica la distancia percibida.
  Clave para ORBE (ficha 05).

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| `RirConvolver` | `app/src/main/cpp/spatial/RirConvolver.hpp` | FFT 1024, 32 particiones de cola, total 16 896 muestras ≈ 352 ms @48 kHz. |
| Dataset | 200 salas | RT60 medido por T20. **El RT60 nominal NO es el real: el WAV mide 2.28× el valor nominal** (`test_rir_dataset`). |
| BRIR sintética | `synthesizeMasterStudioBrir` | RT60 0.34 s por defecto (0.18–1.8 s); reflexiones tempranas 4.2/7.9/13.4 ms; cola de ruido LCG. |
| Parámetros | Wet base 0.22, RT60 0.34 s (Ruta A); wet ≤0.45 (Ruta B) | Duck de voz 75% (−12 dB). |

## Mediciones que lo respaldan

- Dataset de 200 salas con RT60 medido por T20.
- **Advertencia:** `irLen=min(2560, rt60·sr)`: con rt60=0.34 s la IR sintética
  se trunca a 2560 muestras (53 ms), no a los 352 ms que admite el convolver.
- **Sin medición de sala real del usuario:** la sala de Esse no está medida.

## Preguntas abiertas (para la evolución)

1. **Sala diferenciable (FRONTERA §2.7):** ¿estimar geometría + absorción
   desde una palmada grabada con el micrófono del teléfono, y que la RIR
   estimada prediga la medida mejor que la genérica?
2. ¿RT60 por banda (no global) medido de la sala de Esse con el sensor
   acústico (ficha 04)?
3. ¿C80 como objetivo de claridad en vez de RT60 global?
4. ¿La IR truncada a 53 ms (sintética) suena peor que la completa (352 ms)?

## Hueco 2 cerrado — RT60 de la sala (2026-11-09)

**Estado: LISTO PARA MEDIR.** Código implementado:
`app/src/main/java/com/ivanna/omega/audio/Rt60Measurement.kt` (ráfaga de ruido
blanco 0.5 s → 3.0 s de cola; EDC de Schroeder + EDT/T20/T30/C80 por banda de
octava 125–4000 Hz, con criterios de fiabilidad por rango dinámico). Falta
ejecutar en la sala de Esse en silencio. Detalle en
`docs/KNOWLEDGE_CORE/GAPS_FILLED.md` §2.
