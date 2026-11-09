# KNOWLEDGE CORE — Núcleo de Conocimiento de IVANNA

**Versión:** 2.0 (2026-11-09) · **Estado:** 26 fichas + consulta + mapa de evolución.
**Propósito:** consolidar en un solo lugar consultable todo el conocimiento
neuro-acústico, matemático, musical, eléctrico, psicoacústico, de salas, de
DSP, de codecs, de audición, de ML y de medición que IVANNA ya tiene
fragmentado en informes (`~/workspace/analysis-ivanna/`), docs del repo y
comentarios de diseño en el código. Los agentes (`IvannaAgentCore`), Gemini
(`IvannaGeminiAgent`) y el Motor de Evolución consultan este núcleo para
decidir, aprender y proponer física nueva. **No es un tratado desde cero:**
cada ficha cita su fuente y sus mediciones reales; donde no hay medición,
lo dice y marca qué medición lo validaría.

## Cómo consultar

```bash
# Por palabra clave / tema (devuelve las fichas relevantes, con resumen)
python3 docs/KNOWLEDGE_CORE/query.py "volterra kernel medido"
python3 docs/KNOWLEDGE_CORE/query.py --list            # lista todas las fichas
python3 docs/KNOWLEDGE_CORE/query.py --domain psicoacustica
python3 docs/KNOWLEDGE_CORE/query.py --full hrtf       # ficha completa
python3 docs/KNOWLEDGE_CORE/query.py --context "física nueva"  # texto listo para Gemini
```

Para Gemini: `query.py --context "<tema>"` devuelve el texto concatenado de
las fichas relevantes, listo para pegar como contexto del prompt de sistema.

## Dominios

| Dominio | Fichas |
|---|---|
| **A. Psicoacústica / neuro-acústica** | 01 HRTF, 08 ERB, 09 Back-EMF, 11 NPE/LIF, 13 Enmascaramiento, 14 Sonoridad, 15 Localización/Haas, 25 Pitch/Timbre, 26 Fatiga/MOC |
| **B. Matemáticas / DSP** | 02 Volterra, 03 HOLOS, 04 Phi, 05 ORBE, 06 ASRC, 10 EvolutionaryEQ, 17 Filtros adaptativos, 18 Compresión multibanda, 19 Dithering/Oversampling |
| **C. Música / percepción** | 07 Harman 2018, 12 VISCERAL/ABX |
| **D. Acústica de salas** | 16 Salas (RT60/EDT/C80) |
| **E. Codecs y transporte** | 20 Codecs/transporte |
| **F. Ciencia de la audición** | 21 Anatomía del oído |
| **G. ML para audio** | 22 YAMNet/ML, 24 Separación de fuentes |
| **H. Medición y validación** | 23 THD+N/IMD/SNR |
| **I. Frontera (física nueva)** | Ver `EVOLUTION_MAP.md` + `FRONTERA.md` (7 direcciones) |

## Índice de fichas (26)

| # | Ficha | Dominio | Concepto en una línea |
|---|---|---|---|
| 01 | [HRTF](fichas/01-hrtf.md) | A | La cabeza como filtro: ITD/ILD/pinna codifican dirección |
| 02 | [Volterra](fichas/02-volterra.md) | B | La no-linealidad con memoria del transductor, medida por banda |
| 03 | [HOLOS](fichas/03-holos.md) | B | Un solo operador: G·K·Γ por banda, toda la cadena en uno |
| 04 | [Phi](fichas/04-phi.md) | B | HOLOS muestra a muestra, sin FFT: neuro + EMF + campo en un estado |
| 05 | [ORBE](fichas/05-orbe.md) | B | Binaural esférico de campo cercano: distancia continua por banda |
| 06 | [ASRC / deriva](fichas/06-asrc-deriva.md) | B | El reloj del DAC deriva; el servo ASRC la corrige en el write-stage |
| 07 | [Harman 2018](fichas/07-harman-2018.md) | C | La curva objetivo con respaldo experimental (preferencia media) |
| 08 | [ERB](fichas/08-erb.md) | A | La escala de bandas críticas de la cóclea (Glasberg & Moore) |
| 09 | [Back-EMF](fichas/09-back-emf.md) | A | El altavoz habla de vuelta: su impedancia revela su estado |
| 10 | [EvolutionaryEQ](fichas/10-evolutionary-eq.md) | B | Un genoma espectral que evoluciona hacia Harman (LM-CMA-ES) |
| 11 | [NPE / LIF](fichas/11-npe-lif.md) | A | Un modelo de cóclea + neuronas LIF que actúa por muestra |
| 12 | [VISCERAL + ABX](fichas/12-visceral-abx.md) | C | Cinco dimensiones medibles + ABX ciego que decide qué es real |
| 13 | [Enmascaramiento](fichas/13-enmascaramiento.md) | A | Un sonido fuerte hace inaudible a otro: frecuencial y temporal |
| 14 | [Sonoridad](fichas/14-sonoridad.md) | A | Lo fuerte que se percibe: LUFS, ISO 226, ponderación K |
| 15 | [Localización / Haas](fichas/15-localizacion-haas.md) | A | ITD/ILD/pinna + precedencia: el cerebro localiza por el primero |
| 16 | [Acústica de salas](fichas/16-acustica-salas.md) | D | RT60, EDT, C80, modos: la sala como filtro |
| 17 | [Filtros adaptativos](fichas/17-filtros-adaptativos.md) | B | LMS/NLMS/RLS + beamforming: el filtro que aprende en vivo |
| 18 | [Compresión multibanda](fichas/18-compresion-multibanda.md) | B | Atenuar lo fuerte por banda sin aplastar los transitorios |
| 19 | [Dithering / oversampling](fichas/19-dithering-oversampling.md) | B | Ruido que limpia + procesar a 2× para que no se pliegue nada |
| 20 | [Codecs y transporte](fichas/20-codecs-transporte.md) | E | PCM, Opus, LDAC, aptX, UAC2, A2DP: cada eslabón tiene su precio |
| 21 | [Anatomía del oído](fichas/21-anatomia-oido.md) | F | Pabellón, tímpano, cóclea, IHC/OHC: el hardware que hay que imitar |
| 22 | [YAMNet y ML](fichas/22-yamnet-ml-audio.md) | G | La red que dice qué suena: 521 clases, embeddings, TinyML |
| 23 | [Medición THD+N/IMD/SNR](fichas/23-medicion-thdn.md) | H | Cómo saber si algo suena mejor: medirlo con nivel y frecuencia |
| 24 | [Separación de fuentes](fichas/24-separacion-fuentes.md) | G | Descomponer la mezcla en vocales/batería/bajo con redes U-Net |
| 25 | [Pitch y timbre](fichas/25-pitch-timbre.md) | A | Lo agudo/grave (f0) y lo que distingue un piano de una guitarra |
| 26 | [Fatiga / MOC](fichas/26-fatiga-moc.md) | A | El freno del oído: el cerebro baja la ganancia coclear |

## Reglas del núcleo (para quien lo use)

1. **Nada se afirma sin medición o fuente.** Cada ficha separa lo medido
   (con número y dónde) de lo supuesto (marcado como tal). Si un concepto no
   tiene medición en IVANNA aún, la ficha marca qué medición lo validaría.
2. **Fail-closed.** Si el conocimiento no existe o no está verificado, la ficha
   lo dice y el agente no inventa.
3. **Una ficha, un concepto, un dueño.** Cada ficha cita dónde vive el concepto
   en el código (archivo/línea) y qué test lo cubre.
4. **Las preguntas abiertas son el combustible de la evolución.** Cada ficha
   termina con preguntas que el Motor de Evolución debe intentar responder
   (ver `EVOLUTION_MAP.md`).
5. **No se toca la seguridad.** El núcleo no contiene detalles de la
   protección anti-piratería (integridad, ofuscación, pins). Esos viven solo
   en el código y en la memoria del propietario.

## Fuentes principales (de dónde salió cada ficha)

- `~/workspace/analysis-ivanna/VEREDICTO.md` — ranking de funciones, cadena real.
- `~/workspace/analysis-ivanna/FUSION.md` — HOLOS, los 12 núcleos matemáticos.
- `~/workspace/analysis-ivanna/FRONTERA.md` — 7 direcciones, índice VISCERAL, ABX.
- `~/workspace/analysis-ivanna/BUCLE_CERRADO_NEURAL.md` — NPE, ASSR, escalas de tiempo.
- `~/workspace/analysis-ivanna/SONDA_PAL_2026-11-07.md` — deriva medida, PAL, ASRC.
- `~/workspace/ivanna/docs/HYPER_STATE_OPERATOR.md` — Phi.
- `~/workspace/ivanna/docs/EVOLUTIONARY_EQ.md` — EvolutionaryEQ.
- `~/workspace/ivanna/docs/GENEZI_MHC_PZ1D_KERNEL.md` — kernel Volterra medido.
- `~/workspace/ivanna/app/src/main/cpp/spatial/EliteTargetCurves.hpp` — Harman 2018.
- Literatura citada en cada ficha (Glasberg & Moore, Olive et al., Smalt et
  al., Blauert, Sabine, Schroeder, Défossez et al., ITU-R BS.1770, ISO 226).
