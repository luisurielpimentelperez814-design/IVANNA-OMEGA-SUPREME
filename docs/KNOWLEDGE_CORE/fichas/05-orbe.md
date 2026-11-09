# Ficha 05 — ORBE (Binaural Esférico de Campo Cercano)

**Dominio:** B (matemáticas/DSP) · **Estado:** en producción, activo por defecto
**Fuentes:** `VEREDICTO.md` §5; `FUSION.md` §1.4; `IMPLEMENTACION_ORBE.md`.

## Concepto

Render binaural en el que cada banda de frecuencia se coloca a un **azimut y
una distancia continuos (0.15–2.0 m)** usando el modelo de cabeza esférica en
campo cercano, con las claves espaciales **medidas de la propia grabación**
en vez de impuestas, y con parallax real ante el movimiento de la cabeza.
Ningún producto de consumo combina campo cercano continuo por banda +
estimación de escena desde estéreo sin metadata + parallax de campo cercano.

## Definición y fórmulas

- **Cabeza esférica (Rayleigh/Woodworth generalizado):** radio `a = 0.0875 m`,
  `c = 343 m/s`. El ITD y el ILD dejan de ser constantes al acercarse:
  `ITD = (pathL − pathR)/c` (→ Woodworth `(a/c)(θ+sinθ)` cuando `r→∞`;
  **+~12% a 0.15 m/90°**). `ILD = 20·log10(dL/dR) + 20·log10(1+0.25·(ka)²·sin²θ)`.
- **Efecto de proximidad esférico:** `proximityShelf = min(20·log10(1/r), 6 dB)
  · 1/(1+(f/300)²)`, 0 si `r ≥ 1 m`. Hasta +6 dB en graves en el oído ipsilateral.
- **Estimación por banda ERB (24 bandas, 60 Hz–16 kHz):** ILD, IPD→ITD,
  coherencia, DRR estimada. Azimut por **bisección (24 iter)** invirtiendo
  `itdSeconds` si `f_c<700 Hz` o `ildDb` si `f_c≥700 Hz`; distancia por
  bisección invirtiendo `ildDb` solo si `f_c<800 Hz` y `coh>0.5`. Suavizado
  one-pole ~20 ms.
- **Render por bin:** retardo = rotación de fase exacta `e^(∓jω·ITD/2)`;
  ganancia por ILD + shelf; magnitud pinna = HRTF medida interpolada
  (solo >1 kHz); difuso (coherencia < umbral) → al RIR existente.
- **Parallax:** con `r = 0.2 m`, una rotación pequeña de cabeza cambia el
  azimut aparente ~5× más que a 1 m (`Δθ ≈ atan(d·sinθ/r)`).
- Dominio: STFT Hann, **hop 128 fijo, FFT 512 @≤48 kHz** (1024 @96k, 2048
  @192k, 4096 @384k). Latencia = fftSize = **512 muestras = 10.7 ms @48 kHz**.

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| `NearFieldHeadModel.hpp` | `app/src/main/cpp/spatial/NearFieldHeadModel.hpp` | `itdSeconds(θ,r)`, `ildDb(θ,r,f)`, `proximityShelfDb(r,f)` — funciones puras. |
| `OrbeBinaural.hpp/.cpp` | `app/src/main/cpp/spatial/OrbeBinaural.hpp/.cpp` | Núcleo: STFT, estimación, render, overlap-add. |
| Parámetros | amount 1.0, distance 0.35 m, spread 1.0, proximity 3 dB, diffuse 0.5 | Activo por directiva de Esse (anula puerta GREEN para este release). |

## Mediciones que lo respaldan

- **THD+N −139.33 dB @1 kHz** encendido (limpio, sin retuneo).
- Throughput host **23× RT**. 21 tests host.
- Fail-closed: coherencia media <0.3 o shed térmico ⇒ wetTarget=0.
- **Limitación declarada:** no modela magnitud de pinna; solo parallax de
  rotación (yaw). Mono intacto (L==R → salida = entrada).

## Preguntas abiertas (para la evolución)

1. ¿Añadir torso + hombro (FRONTERA §2.3) como tercer término de la función
   de Green? El eco de hombro es la única señal de elevación <3 kHz.
2. ¿Magnitud de pinna por banda (no solo parallax) con OT (FRONTERA §2.1)?
3. ¿Distancia por banda con DRR medida (no solo ILD en graves)?
4. ¿ORBE a 384 kHz sin shed térmico (FFT 4096, coste 8×)?
