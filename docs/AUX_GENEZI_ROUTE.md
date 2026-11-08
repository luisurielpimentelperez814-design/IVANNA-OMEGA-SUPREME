# Ruta AUX Genezi / auto — v2.5.366

**Salida por altavoces frontales a través de DAC USB-C + Auxiliar 3.5 mm**
(Sony Genezi MHC-PZ1D en habitación, o autoestéreo en habitáculo).

Este documento dice qué se construyó, qué se rechazó y por qué. Léelo
antes de pedir "más" de esta ruta: los límites aquí son física, no
falta de ambición.

## Lo construido (real, cableado, testeado)

**`app/src/main/cpp/spatial/AuxGeneziRoute.hpp`** (header-only, hilo de
control, cero malloc en audio):

| Pieza | Qué hace | Dónde vive en la cadena |
|---|---|---|
| `auxPreset(GeneziRoom)` | XTC span 0.90 m / dist 0.85 m / offset 0; AUX xover 120 Hz, coherencia 1.0 | `IvannaFusionEngine::setAuxGeneziPreset(0)` (Ruta B), `nativeSetAuxGeneziPreset(0)` (Ruta A) |
| `auxPreset(CarCabin)` | XTC span 1.40 m / dist 0.80 m / offset −0.30 m (conductor LHD); AUX xover 100 Hz, coherencia 1.0 | `setAuxGeneziPreset(1)` / `nativeSetAuxGeneziPreset(1)` |
| `AuxVolterraGate::validate()` | Acepta SOLO paquete IVK1 con dataset `GENEZY01`, K=64, fs 48 kHz, dispositivo `MOTO_G85_GENEZY`, CRC y presupuestos H2 válidos. Rechaza dataset público `DATASET0` siempre, sintético `SYNTH000` sin flag, K distinto, dispositivo distinto, truncado. | Puerta de carga del kernel medido; el motor Volterra H2 (`VolterraH2Symmetric`, K=64) ya existe en ambas rutas |
| `volterraH2Cost(K)` | Coste O(K²) por muestra, en código y en tests | Test `K4096EsInviableEnTiempoReal` |
| `AuxSensingLimits` | Constantes de lo que el stack puede / no puede sensar | Tests |

**Cableado:**
- Ruta B: `IvannaFusionEngine::setAuxGeneziPreset(int)` — geometría XTC
  (`m_xtc.setGeometry`) + campo AUX (`setAuxField`). Un snapshot WFS
  posterior puede reescribir la geometría XTC; el preset se reaplica
  llamando de nuevo.
- Ruta A: `nativeSetAuxGeneziPreset(int)` — geometría XTC a la cadena 4D
  (`fourd::setSpeakerGeometry`, misma vía que `routeAApplyXtcGeometry`)
  + atomics AUX + daemon + snapshot, igual que `nativeSetAuxField`.
- Kotlin: `IvannaNativeLib.nativeSetAuxGeneziPreset(preset: Int)`.

**Calibración de entorno real:** `RoomProbe` (micrófono del teléfono,
barrido lock-in 50 Hz–6 kHz, corrección acotada −9/+3 dB) + `setRoomCorrection`.
Es la única medición de entorno del stack. El kernel Volterra medido del
sistema real se carga por el protocolo documentado en
`docs/research/volterra_h2_public_dataset/GENEZY_CHARACTERIZATION_PROTOCOL.md`
(dataset `GENEZY01`).

**Tests host:** `tests/test_aux_genezi_route.cpp` — 6/6 PASSED, ASan+UBSan
limpios. `test_neuro_acoustic_closed_loop` sigue 11/11.

## Los 5 pilares del encargo — veredicto

### 1. Volterra K=4096 de memoria extendida — NO viable en tiempo real
H2 es O(K²) por muestra. K=4096 → 8,390,656 coeficientes H2 (33 MB) y
~8.4 M MACs/muestra/canal → ~806 GMAC/s en estéreo @48 kHz. Ningún
teléfono lo hace en tiempo real. El límite real es K=64 (2,080 coef. H2,
~4,160 MACs/muestra, ~0.2 GMAC/s) con kernel **medido** del sistema
(`GENEZY01`). Sin kernel medido, el motor queda en identidad — nunca se
inventa un kernel.

### 2. Back-EMF por el cable Auxiliar — NO posible
La salida Aux 3.5 mm es analógica y de **solo salida** (DAC → amplificador
→ altavoz). No hay camino de retorno para la fuerza electromotriz del cono,
el chip del DAC no mide impedancia de carga, y USB Audio Class no expone
sensado de carga. Entre el DAC y el altavoz hay un amplificador que rompe
cualquier lazo eléctrico. La calibración real es por micrófono (RoomProbe).

### 3. "Colapso de paredes" no euclidiano — NO posible
Dos fuentes frontales no pueden cancelar las reflexiones primarias de
techo / vidrios / paredes en todas las posiciones del oyente: la
cancelación destructiva solo es exacta en el punto donde se mide. Lo real:
XTC de diafonía directa (punto dulce, geometría medida) + graves coherentes
en fase + EQ de sala medido. La "percepción de espacio infinito" es
psicoacústica (WFS/HRTF), no cancelación física de reflexiones.

### 4. Elevación por altavoces / acoplamiento coclear — solo en parte
Las pistas de elevación por pinna sí se pueden codificar en la señal
(HRTF con datasets KEMAR/CIPIC medidos), pero por altavoces son débiles y
dependen del oyente y de la sala; no hay "engaño directo al SNC". Además,
en Ruta B el wet HRTF se fuerza a 0 cuando AUX está activo
(`IvannaFusionCore::process()`: el HRTF binaural es para audífonos). La
elevación por altavoces requeriría un módulo propio (pinna-EQ espectral),
que no existe aún y no se finge. El NPE coclear sigue en la cadena como
modelado, no como interfaz neural.

### 5. Zero-PCM / DSD512 directo al DAC — NO existe en este stack
Android AudioFlinger y USB Audio Class son PCM, y la salida Aux 3.5 mm es
analógica **después** del DAC: no hay bitstream que entregar por ese cable.
Lo real: filtros de fase mínima (sin pre-ringing de FIR de fase lineal),
bloques de 128 muestras, y la ruta de alta frecuencia que el HAL permita.

## Lo que falta (trabajo futuro, no autorizado aún)
- Medición real del Genezi / habitáculo con el protocolo
  `GENEZY_CHARACTERIZATION_PROTOCOL.md` → paquete IVK1 `GENEZY01` K=64.
- Módulo pinna-EQ espectral para elevación por altavoces.
- Offset RHD (+0.30 m) como parámetro, y UI para elegir el preset.
- Medición en el Moto G85 (este VM no tiene SDK Android; la build
  Kotlin/NDK se valida en CI).

## Versión
v2.5.366 (656 → 657). Commit y tag locales; push a GitHub pendiente de
que Uriel conecte la autenticación (el token pegado en el chat no se usa
ni se guarda: revocar).
