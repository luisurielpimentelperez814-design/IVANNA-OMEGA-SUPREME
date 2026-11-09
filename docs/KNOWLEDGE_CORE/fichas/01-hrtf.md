# Ficha 01 — HRTF (Head-Related Transfer Function)

**Dominio:** A (psicoacústica) · **Estado:** medido y en producción
**Fuentes:** `VEREDICTO.md` §3#1, §5; `FUSION.md` §1.1; `FRONTERA.md` §2.1, §2.3, §2.5.

## Concepto

La cabeza, las orejas y el torso filtran el sonido antes de que llegue al
tímpano. Ese filtro depende de la dirección de la fuente y es distinto para
cada oído. La HRTF es ese filtro, medido: un par de respuestas al impulso
(HRIR izquierda/derecha) por dirección. Con ella, un par de auriculares
puede colocar una fuente virtual en cualquier dirección.

## Definición y fórmulas

- **ITD (diferencia interaural de tiempo):** el sonido llega antes al oído
  cercano. Modelo de Woodworth (cabeza esférica, radio `a = 0.0875 m`,
  `c = 343 m/s`):
  `ITD(θ) = (a/c)·(sinθ + θ)`, máximo ≈ **0.655 ms** a 90°.
- **ILD (diferencia interaural de nivel):** la cabeza hace sombra al oído
  lejano. Casi nulo en graves en campo lejano; crece con la frecuencia y
  con la cercanía (hasta 15–20 dB a 0.15 m, ver ficha ORBE).
- **Pinna (pabellón):** la oreja introduce notches espectrales en **6–12 kHz**
  cuya posición depende de la elevación. Es la única señal de elevación en
  campo lejano.
- **Sombra cefálica (crossfeed natural):** copia ×0.12 retardada ~290 µs
  (14 muestras @48 kHz) en el canal contralateral.

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| `HrtfManager` | `app/src/main/cpp/HrtfManager.hpp/.cpp` | FIR directa 2×2, 128 taps, crossfade 128 bloques (~341 ms). Datasets medidos KEMAR + 7 CIPIC humanos (`.ihr1`, SHA-256) con fallback sintético (modelo esférico de Rayleigh). |
| `HRTFConvolver` | `app/src/main/cpp/spatial/hrtf_convolver.hpp/.cpp` | IR 512 taps, FFT 1024, bloque 256. Usado por los 8 altavoces virtuales del decoder HOA. Entrada colapsada a mono. |
| `HoaBinauralDecoder` | `app/src/main/cpp/spatial/HoaBinauralDecoder.cpp` | 8 altavoces virtuales, pesos max-rE [1, 0.866, 0.5]. |
| Crossfeed ITD graves | `app/src/main/cpp/dsp/HeadphoneCrossfeed.hpp` | Woodworth 0.261 ms @30°, LP 700 Hz, g máx 1/3. Apagado por defecto. |

**Advertencia real:** hay DOS motores HRTF distintos (128 taps vs 512 taps) y
en régimen estacionario son alternativas mezcladas por envolvente, no cascada.
El decoder no propaga el perfil HRTF → cada convolver cae a su fallback
sintético interno.

## Mediciones que lo respaldan

- Azimuts opuestos dan salidas distintas; sin NaN (`test_hrtf_convolver_rt_safety`).
- HRTF genérica por defecto (KEMAR); sin medición individual del oyente.
- HUTUBS (96 sujetos): la interpolación con alineación temporal + corrección
  de magnitud reduce el error medio hasta 1.5 dB (4 dB contralateral).
- La individualización por BEM mejora la localización (+15° modelo, +183%
  tasa de acierto de azimut en un estudio), pero el head-tracking es más
  eficaz que la individualización para reducir reversiones.

## Preguntas abiertas (para la evolución)

1. ¿La interpolación por transporte óptimo (Wasserstein/Sinkhorn) preserva
   los notches de pinna mejor que la lineal? (FRONTERA §2.1 — falsable con
   HUTUBS: error de posición del primer notch.)
2. ¿Una HRTF personal por BEM desde foto de oreja supera a KEMAR en
   elevación en ≥2 de 3 sujetos? (FRONTERA §2.5.)
3. ¿Cómo unificar los dos motores HRTF en uno solo sin romper la cadena?
4. ¿Qué pierde el decoder al colapsar a mono antes de la convolución?
