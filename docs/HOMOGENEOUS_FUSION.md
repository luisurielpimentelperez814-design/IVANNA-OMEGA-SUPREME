# Fusión Homogénea Φ — v2.5.367

**El Hiper-Operador de Estado: un solo estado, un solo `update()` por bloque.**

Este documento dice qué es Φ, qué no es, cómo se integra, y qué hay de
real detrás de los cuatro paradigmas de la siguiente etapa. Léelo antes
de pedir "más allá": los límites aquí son física, medicina y derecho,
no falta de ambición.

## 1. El operador Φ (lo construido)

`app/src/main/cpp/spatial/HomogeneousFusion.hpp` (header-only, hilo de
audio RT-safe, cero malloc en `update()`).

Φ es una **composición** de operadores reales que ya existen, en el
orden en que ya corren en la cadena:

```
Φ = Γ ∘ K ∘ G ∘ XTC ∘ AUX ∘ N
```

| Término | Módulo real | Estado |
|---|---|---|
| N | `ClosedLoopController` + `NeuroBandEq` (v2.5.365) | Firma cortical por usuario → ganancia/fase por banda Greenwood + ajustes HOLOS/ORBE, slew-limitado, fail-closed |
| AUX | `AuxFieldCoherence` (graves coherentes, LR4) | Preset de ruta v2.5.366 |
| XTC | `CrosstalkCanceller` (diafonía directa) | Geometría del par de altavoces (preset) |
| G | `HolosGreenKernel` | `Y_b = G_b ⊛ [K_b · X_b] · Γ_b` (v2.5.364) |
| K | `HolosVolterraKernel` / `VolterraH2Symmetric` | K=64, kernel MEDIDO `GENEZY01`; sin kernel, identidad |
| Γ | `HolosGainField` | Ganancia única estática × dinámica |

`HomogeneousFusion` es el punto único de decisión: `prepare()`,
`setRoutePreset()`, `ingestNeuro()`, `loadVolterraPackage()` (puerta
IVK1), `emfVector()`, y `update(L, R, n, base, adjustedOut, nowUs)`
1× por bloque antes de HOLOS/ORBE. Devuelve `FusionStatus`: qué
vectores están vivos (neuro activo/fresco/wet, Volterra listo, preset
de ruta, AUX activo, EMF disponible = siempre false).

### Lo que Φ NO es (y por qué se dice aquí)

- **No es un tensor continuo sin discretizar.** La señal sigue siendo
  PCM en bloques de 128 muestras y HOLOS trabaja en STFT (hop 128 /
  FFT 512, 10.7 ms @48 kHz). No hay procesamiento "sin Fourier por
  bloques" ni "sin PCM": AudioFlinger y USB Audio Class son PCM, y el
  Aux 3.5 mm es analógico después del DAC.
- **No hay "inversa matemática exacta" del ambiente.** La
  pre-distorsión Volterra corrige lo que el kernel medido identifica,
  dentro de sus presupuestos de estabilidad; fuera de eso, identidad.
- **No hay pistones de masa cero ni desaparición del hardware.** La
  física de la caja se compensa parcialmente con un kernel medido, no
  se anula.

## 2. Los tres vectores de entrada, estado honesto

- **S_neuro(t) — REAL cuando hay driver.** Frames EEG/ABR/fNIRS por
  `ingestNeuro()`; sin driver no llegan frames y N es identidad.
  Latencia real del bucle: frame de sensor (~10 ms) + STFT HOLOS
  (10.7 ms). No microsegundos.
- **Z_emf(t) — NO DISPONIBLE.** Back-EMF por Aux es imposible
  (`docs/AUX_GENEZI_ROUTE.md` §2): salida analógica de solo salida,
  sin camino de retorno, DAC sin sensado de carga. `EmfVector` existe
  como interfaz con `kAvailable = false` para que ningún código futuro
  pueda fingir una medida. La identificación de planta real es
  `RoomProbe` (micrófono del teléfono, 50 Hz–6 kHz).
- **W(r,θ) — REAL y acotado.** Geometría XTC + campo AUX del preset
  de ruta. La apertura es la del par de altavoces y su geometría, no
  infinita; las paredes no colapsan (v2.5.366 §3).

## 3. Integración (archivos completos, listos para cablear)

El módulo es autocontenido y testeado (7/7 host, ASan+UBSan). Para
adoptarlo en las rutas, sustituye las dos llamadas sueltas por una:

**Ruta B** (`IvannaFusionEngine`, donde hoy vive `updateNeuroLoop()` +
`setAuxGeneziPreset()`):
```cpp
#include "spatial/HomogeneousFusion.hpp"
ivanna::spatial::HomogeneousFusion fusion_;
// en el ctor / initSpatial(): fusion_.prepare(sampleRate, BLOCK_SIZE);
// control: fusion_.setRoutePreset(preset); fusion_.setEnabled(true);
//          fusion_.ingestNeuro(frame); // desde el gateway, como hoy
//          fusion_.loadVolterraPackage(pkg, size); // kernel medido
// audio, 1× por bloque antes de HOLOS/ORBE (donde hoy va updateNeuroLoop):
ivanna::neuromorphic::ClosedLoopTargets hp = baseHolosOrbe; // de g_holos_*/g_orbe_*
const auto st = fusion_.update(buffer->left, buffer->right, BLOCK_SIZE, base, hp);
// hp ya lleva los ajustes neuro; XTC/AUX se leen de fusion_.routeParams()
// y HOLOS usa hp como hoy usa los objetivos ajustados.
```
**Ruta A** (`ivanna_omega_jni.cpp`): mismo patrón con los estáticos
`g_fusionRouteA` (como `g_neuroLoopRouteA` en v2.5.365) y la JNI
`nativeSetAuxGeneziPreset` ya existente para el preset; añadir
`nativeFusionIngest` / `nativeFusionLoadVolterra` solo cuando haya
driver y kernel medido que enchufar. No se cableó en v2.5.367 a
propósito: el coordinador sustituye piezas que hoy funcionan, y
duplicar la ganancia neuro (fusion + bucle viejo a la vez) sería un
bug. La migración es un cambio de 1 archivo por ruta, con los tests
de este módulo como red.

## 4. Los cuatro paradigmas siguientes — qué hay de real

Investigación revisada 2026-11-08. Ninguno es implementable en una app
de audio Android; todos son medicina, neurociencia o especulación. Se
documentan para que el roadmap no confunda deseo con plan.

### 4.1 Transducción no mecánica (bypass del aire y del tímpano)
- **Ultrasonido focalizado transcraneal (tFUS / TANM): REAL como
  investigación clínica, NO como interfaz de audio.** La UCSF abrió
  en octubre de 2026 el ensayo fase 1 MESSI-LEO (NCT07710989) con una
  diadema de ultrasonido wearable para modular el estriado en
  alucinaciones auditivas de esquizofrenia; NYU publicó un piloto de
  una sesión de 10 minutos con reducción de delirios. La revisión
  TANM 2025 sistematiza la evidencia 2021–2025: los efectos dependen
  de la frecuencia de repetición de pulso y el ciclo de trabajo, no
  de la portadora. Nadie "escucha música" por tFUS: es
  neuromodulación (excitar/inhibir una región), no un canal auditivo
  con timbre, dinámica y escena 3D. Es un dispositivo médico en
  ensayos, no un periférico de teléfono.
  Fuentes: UCSF / TechTimes 2026-10-31
  <https://www.techtimes.com/articles/329802/20261031/ucsf-opens-trial-wearable-ultrasound-silenced-voices-all-3-schizophrenia-patients.htm>;
  revisión TANM 2025 <https://www.aikalab.ai/research/jGFhg8frdkCU0Le2JzoJ>.
- **Optogenética auditiva / implante coclear óptico (oCI): REAL como
  investigación preclínica, NO como producto.** El grupo de Moser
  (Gotinga) demostró desde Hernandez et al. 2014 (J Clin Invest) que
  la estimulación optogenética del nervio auditivo activa toda la vía
  auditiva; Dieter et al. 2019 (Nat Commun) mostró selectividad
  espectral casi fisiológica; Alekseev et al. 2025 (Nat Biomed Eng)
  reporta respuestas robustas a baja intensidad; en 2026 se demuestra
  codificación temporal casi fisiológica con f-Chrimson en jerbos.
  El precio: hay que volver fotosensibles las neuronas del ganglio
  espiral con **terapia génica (virus AAV que introducen
  channelrhodopsinas)** más un implante óptico. Es cirugía +
  modificación genética, no software. Décadas del audio de consumo.
  Fuentes: Moser, Canadian Audiologist 2026-04-08
  <https://canadianaudiologist.ca/optical-cochlear-implants-recent-progress-toward-light-based-hearing-restoration/?output=pdf>;
  Instituto de Neurociencia Auditiva de Gotinga
  <http://www.auditory-neuroscience.uni-goettingen.de/hearing_the_light_EN.html>;
  f-Chrimson 2026 <https://www.biorxiv.org/content/10.64898/2026.05.16.724905v2>.
- **Veredicto:** el "bypass del aire" existe en laboratorio como
  prótesis auditiva invasiva o neuromodulación médica. Ninguna de las
  dos cabe en IVANNA, y ninguna debería prometer "qualia auditivo
  puro" sin tímpano: lo que se ha demostrado es activación neural
  medible, no experiencia musical.

### 4.2 Teleportación perceptual de campos acústicos
No existe como tecnología. Lo más cercano real es captura ambisónica
(HOA) remota + streaming binaural: transmitir el campo de una sala
real a unos audífonos es ingeniería existente (HOA + HRTF). La
"fusión espacial multilocal sin solapamiento destructivo" (Notre Dame
+ bosque tropical a la vez, interactuando) no es un problema de
ingeniería definido: dos campos superpuestos en el mismo aire se
suman, y separarlos perceptualmente es mezcla de fuentes, no
teleportación. Es una metáfora, no un roadmap.

### 4.3 Expansión cognitiva / audición hiperdimensional
La **sonificación de datos** es real (p. ej., sonificación de datos
astronómicos de la NASA: flujos de datos → sonido audible). Lo que
no existe es evidencia de que la corteza auditiva pueda aprender a
"oír" dimensiones adicionales (4D/5D) como percepción espacial
intuitiva. La sustitución sensorial (p. ej., ver a través del sonido)
tiene décadas de investigación con resultados modestos y específicos.
" cortex extendido" es especulación neurocientífica, no un módulo.

### 4.4 Sincronización inter-cerebral colectiva
Las interfaces cerebro-cerebro existen en investigación (p. ej.,
BrainNet, Univ. de Washington 2019: varias personas colaboran por
EEG/TMS en una tarea simple). Un "espacio colectivo de fase
sincronizada" con resolución sub-milimétrica, adaptado a la
biometría neuronal individual en tiempo real, no existe ni como
experimento. Además: sincronizar cerebros requeriría estimulación o
lectura neural invasiva o semi-invasiva en cada participante, con
implicaciones médicas, éticas y legales que exceden por completo un
motor de audio.

## 5. Versión
v2.5.367 (657 → 658). Módulo + tests + este doc; integración en rutas
documentada en §3 (pendiente de decisión, no de código). Commit y tag
locales; push a GitHub pendiente de que Uriel conecte la
autenticación (el token pegado en el chat no se usa ni se guarda:
revocar).
