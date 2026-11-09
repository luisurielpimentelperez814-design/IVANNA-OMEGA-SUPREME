# Ficha 19 — Dithering y oversampling

**Dominio:** Procesamiento de señal avanzado · **Estado:** usado (oversampling 2× en exciter)
**Fuentes:** `VEREDICTO.md` §2.1 paso 10; `FUSION.md` §1.5.

## Concepto

**Oversampling:** procesar a un múltiplo de la frecuencia de muestreo para
que los artefactos no lineales (armónicos del waveshaper) caigan fuera de la
banda audible y se filtren, en vez de plegarse (aliasing) dentro de ella.
**Dithering:** añadir ruido de muy bajo nivel (TPDF, ±1 LSB) antes de
cuantizar, para que el error de cuantización sea ruido blanco (inaudible)
en vez de distorsión correlacionada con la señal (audible). Sin dither, un
fade-out a 16 bits suena "escalonado"; con dither, suena limpio hasta el silencio.

## Definición y valores

- **Oversampling 2×:** el HarmonicExciter interpola linealmente a 2× antes
  del shaper Chebyshev; los armónicos por encima de Nyquist/2 se filtran al
  bajar. Coste: 2× muestras procesadas.
- **Aliasing del waveshaper:** un polinomio de orden N genera armónicos hasta
  N×f; sin oversampling, los que superan Nyquist se pliegan. El pre-filtro
  del exciter (LPF 8 kHz) limita la banda que entra al shaper.
- **Dither TPDF:** ruido triangular de ±1 LSB (varianza 1/6 LSB²). El
  dither correcto NO se oye: su nivel es ~−93 dBFS a 16 bits.
- **Noise shaping:** mover el ruido de dither a frecuencias donde el oído
  es menos sensible (>10 kHz). Mejora el SNR percibido ~10 dB a 16 bits.
- **Regla:** dither SOLO en la última cuantización (a 16 bits o al truncar);
  nunca ditherizar dos veces.

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| `HarmonicExciter` | `app/src/main/cpp/dsp/HarmonicExciter.cpp` | Oversampling 2× por interpolación lineal; pre-LPF 8 kHz + HPF 3 kHz. |
| **No existe:** | — | Dither explícito en la salida (la cadena es float hasta el HAL; la cuantización la hace AudioFlinger/HAL, no IVANNA). |

## Mediciones que lo respaldan

- Exciter: THD+N −76.67 dB @1 kHz, −106.40 dB @100 Hz (cadena núcleo host).
- **Sin medición de aliasing del shaper con/sin oversampling en IVANNA.**

## Preguntas abiertas (para la evolución)

1. ¿Oversampling 4× en el exciter (en vez de 2×)? ¿Cuánto baja el aliasing
   medido y cuánto sube el coste?
2. ¿Dither con noise shaping en la salida de IVANNA (antes del HAL) mejora
   el SNR percibido a 16 bits?
3. ¿El Padé [3/2] genera menos aliasing que el Chebyshev a igual drive?
