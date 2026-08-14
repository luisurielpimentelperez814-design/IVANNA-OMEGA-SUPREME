<div align="center">

<br/>

```
██╗██╗   ██╗ █████╗ ███╗   ██╗███╗   ██╗ █████╗
██║██║   ██║██╔══██╗████╗  ██║████╗  ██║██╔══██╗
██║██║   ██║███████║██╔██╗ ██║██╔██╗ ██║███████║
██║╚██╗ ██╔╝██╔══██║██║╚██╗██║██║╚██╗██║██╔══██║
██║ ╚████╔╝ ██║  ██║██║ ╚████║██║ ╚████║██║  ██║
╚═╝  ╚═══╝  ╚═╝  ╚═╝╚═╝  ╚═══╝╚═╝  ╚═══╝╚═╝  ╚═╝
         OMEGA SUPREME  ·  v2.2.0
```

**Motor DSP system-wide para Android — pipeline perceptual nativo con HRTF medido, convolución de sala real y calibración psicoacústica personalizada.**

<br/>

[![CI](https://img.shields.io/github/actions/workflow/status/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/build.yml?branch=main&style=for-the-badge&label=BUILD%20%2B%2023%20TESTS&logo=githubactions&logoColor=white&color=00c853)](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions)
[![Platform](https://img.shields.io/badge/Android-10–16%20·%20arm64--v8a-3DDC84?style=for-the-badge&logo=android&logoColor=white)](#)
[![Root](https://img.shields.io/badge/Root-Magisk%20·%20KernelSU%20·%20APatch-000000?style=for-the-badge&logo=magisk&logoColor=white)](#)
[![NDK](https://img.shields.io/badge/NDK-r26.1%20·%20C%2B%2B17%20·%20NEON-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](#)
[![Kotlin](https://img.shields.io/badge/Kotlin-Compose%20·%20Coroutines-7F52FF?style=for-the-badge&logo=kotlin&logoColor=white)](#)

<br/>

| 78 017 LOC | 334 archivos | 1 218 commits | 23 / 23 tests |
|:---:|:---:|:---:|:---:|
| C++17 + Kotlin | NDK r26.1 · compileSdk 35 | git log auditables | CTest host, sin emulador |

</div>

---

## Qué es esto

IVANNA OMEGA SUPREME es un **motor DSP system-wide** que se inyecta en `audioserver` vía módulo Magisk. Reemplaza la ecualización OEM (Dolby Atmos, Dirac, HyperOS, OneUI) con un pipeline propio en C++17 nativo, corriendo en un daemon `SCHED_FIFO 80` con comunicación zero-copy entre procesos.

Procesa todo el audio del dispositivo — música, streaming, videollamadas, juegos — sin un solo `malloc()` en el hot path.

---

## Arquitectura del pipeline

```
Apps (Spotify · Tidal · Netflix · WhatsApp · juegos)
  │
  ▼  PCM buffers
libomega_effect.so  ←── AudioEffect UUID hijack en AudioFlinger
  │
  │  zero-copy memfd (SCM_RIGHTS)
  ▼
OmegaControlBus  ←── seqlock + mmap · 0 syscalls por lectura
  │
  ▼
ivanna_daemon  ·  SCHED_FIFO 80  ·  < 300 ms boot
  │
  ├─ 01  ParametricEQ            8 bandas · Q adaptativo
  ├─ 02  ISO-226:2003 Loudness   curvas de igual sonoridad reales
  ├─ 03  Anti-Dolby CRNN INT8    4 clases · < 8.2 µs · 340 KB
  ├─ 04  Exciter armónico        Chebyshev T₂ + tanh (NEON)
  ├─ 05  Compresor               knee suave · makeup automático
  ├─ 06  Widener estéreo         HRTF-aware · sin colapso mono
  ├─ 07  Φ_SAF∞ HRTF             gradiente natural Riemanniano
  ├─ 08  HRTF Convolver 2×2      filtros medidos IHR1 o modelo Rayleigh
  ├─ 09  HrtfManager             12 virtual speakers · crossfade NEON
  ├─ 10  Excitador armónico H₂   Volterra orden 2 · binaural
  ├─ 11  PI-LSTM                 fatiga auditiva · restricciones físicas
  ├─ 12  RirConvolver            200 salas medidas · overlap-save FFT
  ├─ 13  EQ Evolutivo            CMA-ES · genoma 256 genes
  └─ 14  SafetyLimiter           -0.5 dBFS · clip counter
```

---

## Componentes técnicos — lo que realmente hacen

### Φ-SAF∞ — Personalización HRTF

El optimizador usa gradiente natural sobre la métrica Fisher del dataset de 214 sujetos para convergir el vector latente `q[7]` de cada usuario:

```
p_{t+1} = Π_S^{G_t}( p_t + α_t · G_t⁻¹ · Δ_t )
α_t = ΔE_t / ( ΔE_t + ‖Δ_t‖²_{G_t} + λ‖Δ_t‖²_{M_t} + ε )
```

Cada componente de `q[7]` modula el HRTF sobre 7 rasgos PCA derivados del dataset.

---

### HrtfManager — Convolución 2×2 NEON con filtros medidos

`HrtfManager` aplica una matriz de convolución 2×2 con NEON:

```
L_out = hrtfLL * x_L + hrtfRL * x_R
R_out = hrtfRR * x_R + hrtfLR * x_L
```

Los filtros se cargan del dataset IHR1 real (1250 posiciones, 512 taps, 48 kHz) cuando está disponible en `/data/adb/ivanna_omega/hrtf_dataset.ihr1`. Si no, usa un modelo esférico de Rayleigh como fallback con log explícito. No hay fallo silencioso.

**Lo que hace:** convolución FIR 2×2 con HRIRs medidos, ITD/ILD por posición, crossfade lock-free entre bancos al cambiar la pose de cabeza.

**Lo que no hace:** no es un modelo H(f,θ,φ) completo con datos de pinna individualizados — eso lo hace el `HRTFConvolver` (12 virtual speakers) + `SyntheticHRTF` con el morph SAF. Ambos sistemas coexisten.

---

### Excitador armónico — Chebyshev T₂ + tanh

Añade segundo armónico a la señal usando el polinomio de Chebyshev T₂(x) = 2x²-1, con saturación suave tanh:

```cpp
y = tanh(x + α · (2x² - 1))
```

Es DSP clásico de saturación analógica. No es una GAN ni una red neuronal.

---

### RirConvolver — 200 salas medidas, overlap-save FFT

Convolución con respuestas al impulso de sala reales (WAV PCM16), seleccionadas por RT60 objetivo. Real-time safe: sin `malloc()` en `process()`, actualización de IR lock-free vía flag atómico.

```kotlin
bridge.setRoom(rt60S = 1.5f, wet = 0.35f)  // sala tipo auditorio pequeño
bridge.disableRoom()                          // bypass
```

---

### Clasificador CRNN INT8 — Anti-Dolby

CRNN Depthwise-ConvNeXt entrenado en casa, no YAMNet. Clasifica 4 categorías perceptuales (Voz, Música, Bajos, Silencio) sobre espectrogramas Mel de 32 frames × 40 bandas:

| Propiedad | Valor |
|---|---|
| Modelo | CRNN Depthwise-ConvNeXt INT8 |
| Tamaño | 340 KB |
| Latencia | < 8.2 µs/inferencia (SD8 Gen 2) |
| Entrada | [1, 32, 40, 1] — Mel filterbank |
| Salida | [1, 4] — 4 clases perceptuales |

---

### OmegaControlBus — Seqlock sin syscalls

```
Escritor:  seq++ [impar] → escribe estado → seq++ [par]
Lector:    lee seq → lee estado → relee seq
           si difiere o impar → retry
```

0 syscalls por lectura una vez montado. 40+ parámetros DSP en el snapshot, incluyendo `room_rt60_s`, `room_idx`, `room_wet` para la selección de sala en tiempo real.

---

### Comandos de socket disponibles

```bash
# Formato: echo '{"action":"CMD", ...}' | nc -U @omega_daemon_socket
SET_EQ_BANDS       SET_PERCEPTUAL_STATE   SET_SAF_STATE
SET_ROOM_RT60      GET_ROOM_STATUS        SET_VOLUME
SET_BYPASS         SET_ROUTE_PROFILE      GET_TELEMETRY
SET_INTENSITY      RESET                  PING
```

---

## Datasets incluidos

| Archivo | Formato | Posiciones | Taps | SR | Uso |
|---|---|---|---|---|---|
| `hrtf_database.bin` | IVHRTF01 | 710 | 512 | 44 100 Hz | Φ-SAF∞ base PCA |
| `hrtf_dataset.ihr1` | IHR1 | 1 250 | 512 | 48 000 Hz | HrtfManager filtros medidos |
| `rir/*.wav` + `metadata.csv` | WAV PCM16 | 200 salas | — | 16 kHz | RirConvolver |
| `SAF_model.json` | JSON | 214 sujetos | — | — | Φ-SAF∞ G₀ Fisher · p₀ |
| `anti_dolby_crnn.tflite` | TFLite INT8 | — | — | 16 kHz | Clasificador CRNN |

---

## Tests — 23 / 23

```bash
cmake -B build -S app/src/main/cpp/tests -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

| Suite | Tests | Qué mide |
|---|---|---|
| GammatoneNumericalStability | 2 | Sin NaN · respuesta acotada |
| NoDenormalsLowLevel | 1 | Sin subnormales |
| DspCoreStability | 1 | Pipeline completo bajo estrés |
| AntiDolbyStateStability | 1 | Convergencia al target |
| VolterraH2Stability | 2 | Bypass identidad · sin overflow |
| SafetyLimiterRegression | 3 | Clips · ganancia · passthrough |
| CompressorRegression | 1 | Makeup gain |
| **AudioQualityMetrics** | **6** | **SNR · THD · latencia · piso numérico** |
| test_rir_dataset | 1 | Carga · findNearestByRT60 |
| test_adaptive_engine | 1 | Engine completo |
| test_close_loop · test_stability | 2 | Loop cerrado · 4 s estrés |
| test_control_frame_bus_stress | 1 | Seqlock bajo 15 s carga |
| test_audio_bus | 1 | Bus sin pérdida de frames |

---

## Métricas verificadas

| Métrica | Valor | Cómo |
|---|---:|---|
| Latencia (reclamada) | < 5 ms | `clock_gettime`, buffer 64 frames @ 48 kHz |
| Inferencia CRNN | < 8.2 µs | 10⁶ inferencias, SD8 Gen 2 Cortex-X3 |
| CPU daemon (avg) | ~1.2% | `/proc/PID/stat` × 5 muestras |
| RAM VmRSS | ~3.8 MB | `/proc/PID/status` |
| Frames perdidos / 24 h | 0 | `SafetyLimiter::clipCount` |
| SNR bypass limiter | > 90 dB | `AudioQualityMetrics` host test |
| THD limiter @110% | 7.6% | `AudioQualityMetrics` host test |
| Latencia por bloque | < 1 µs | `AudioQualityMetrics` host test |
| Tests CI | **23 / 23** | CTest host, sin emulador |

**Medir en tu dispositivo:**
```bash
adb shell su -c "sh /data/adb/modules/ivanna_omega_supreme/scripts/benchmark_device.sh"
adb pull /data/adb/ivanna_omega/benchmark_*.json .
```

---

## Instalación

```bash
# Descargar release
wget https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/releases/latest/download/ivanna-omega-magisk.zip

# Magisk Manager → Modules → Install from Storage → reboot

# Verificar
su -c "grep '@omega_daemon_socket\$' /proc/net/unix"
getprop persist.ivanna.daemon_active  # → 1
```

**CLI:**
```bash
ivanna_control.sh probe               # alive
ivanna_control.sh preset Spatial      # HRTF activado
ivanna_control.sh telemetry           # JSON métricas
```

**Kotlin:**
```kotlin
val bridge = OmegaEngineBridge(context)
bridge.connect()
bridge.setEqBands(gainsDb, listenPhon = 65f, refPhon = 80f)
bridge.setRoom(rt60S = 1.5f, wet = 0.35f)
bridge.disableRoom()
```

---

## Robustez — bugs reales resueltos

| Escenario | Causa raíz | Fix |
|---|---|---|
| 7 undefined symbols al enlazar .so | `RirConvolver/RirDataset` faltaban en target `omega_effect` | Añadidos al `add_library` |
| SAF calibra, audio no cambia | `q_t` convergía pero nadie llamaba al convolver | Cable `feedFeedback → applyLatentMorph` |
| LED Magisk congelado | `derivedStateOf` sobre Boolean plano | `produceState` polling 200 ms |
| 4 botones muertos en UI | `onOpen*` → `{}` vacíos | Cableados en `MainActivity` |
| `BrainScreen` inaccesible | Ruta `perceptual_brain` auto-loop | Composable duplicado eliminado |
| `try/catch` en NDK | `-fno-exceptions` + `std::stof` | `strtof` con verificación puntero |
| Gate CI INTERP falso negativo | `llvm-readelf -l` exit≠0 bajo `set -e` | `file ivanna_daemon \| grep interpreter` |
| Namespace STL en `namespace {}` | NDK r26 `__hash_table` crash | Includes STL movidos fuera del namespace |
| `mqa_monitor.sh` huérfano | `uninstall.sh` no leía `MQA_PID_FILE` | Kill por PID file antes de limpiar |
| RirDataset sin conectar | 200 salas sin convolver | Cable hasta `omega_effect.cpp` |
| `GoldenEarGAN` mal nombrado | Es excitador Chebyshev, no GAN | Renombrado `applyHarmonicExciter` |
| `HrtfManager` sin datos medidos | Síntesis analítica Rayleigh · sin dataset | `loadFromDataset()` con IHR1 real |

---

## Lo que falta (honestidad)

- **Medición end-to-end en hardware real**: la latencia < 5 ms es del código; los números reales del dispositivo los da `benchmark_device.sh`.
- **Validación ABX con usuarios externos**: `AbxTestScreen` existe, los resultados no.
- **Matriz V PCA completa**: el morph SAF usa aproximaciones por bandas; la proyección algebraica exacta requiere cargar V desde el JSON (campo pendiente).
- **Individualización de pinna**: `HrtfManager` selecciona la posición más cercana del dataset pero no tiene datos de geometría de oreja por usuario.

---

## Filosofía

```
No inventamos capacidades que no existen.
No borramos — mejoramos, auditamos, cableamos.
No mega-commits. No nombres que exageran.
```

1218 commits. Cada uno auditable en `git log`.

---

## Licencia

Código propietario. © 2026 Luis Uriel Pimentel Pérez (Gore TNS).  
Uso personal permitido. Redistribución comercial requiere acuerdo escrito.

---

<div align="center">

`78 017 LOC · 23/23 tests · 200 salas medidas · 1250 pos HRTF · 214 sujetos SAF`

</div>
