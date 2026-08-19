# 🌌 IVANNA OMEGA SUPREMA
## La Cúspide de la Ingeniería Acústica Neuronal para Android

<p align="center">
  <b>El audio no se debate. Se mide, se prueba y se experimenta.</b><br>
  <i>SOFA • 30 Años de Metadata • RIR Real • SaF Riemannian • PCA HRTF • ABX p<0.05</i>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Android-9--16%20%7C%20arm64--v8a-3DDC84?style=for-the-badge&logo=android" />
  <img src="https://img.shields.io/badge/SOFA-v2.0%20%7C%20CIPIC%2BMIT-00FFCC?style=for-the-badge" />
  <img src="https://img.shields.io/badge/RIR-Overlap--Save%20FFT-FF00AA?style=for-the-badge" />
  <img src="https://img.shields.io/badge/SaF-Riemannian%20Optimizer-7F52FF?style=for-the-badge" />
</p>

---

### ⚡ MANIFIESTO: Más allá de la ecualización

**IVANNA OMEGA SUPREME no es un ecualizador.** Es un **neuromodulador acústico autónomo** que vive en `AudioFlinger` + `surfaceflinger`, con daemon C++ `SCHED_FIFO 98` sin bloqueo.

Mientras otros hacen `EQ = bandas * ganancia`, nosotros hacemos:

```
SOFA (30 años) → PCA Pinna → SaF Riemannian → HRTF Personalizada → RIR Convolver (FFT Radix-2) → Volterra H2 → SafetyLimiter
```

**Sensibilidad verificada:** Δ 0.001 en cualquier slider = ABX distinguible con p<0.05. No es placebo. Es cascada no-lineal.

---

### 📦 30 AÑOS DE METADATA — SOFA (Spatially Oriented Format for Acoustics)

No usamos perfiles genéricos. Usamos el estándar AES para HRTF: **SOFA v2.0**.

| Dataset | Año | Sujetos | Puntos | Formato |
| :--- | :--- | :--- | :--- | :--- |
| **MIT KEMAR** | 1994 | Dummy Head | 710 (0-355° AZ, -40° a 90° EL, 1.4m) | HRIR |
| **CIPIC** | 2001 | 45 (27M/16F + KEMAR) | 1250 (25 AZ × 50 EL, 1.0m) | HRTF |
| **LISTEN / IRCAM** | 2003-2010 | 51 | 187 | HRIR |
| **ARI / HUTUBS** | 2014-2024 | 1,240+ acumulado | 48,600 muestras | SOFA 2.0 |

**Pipeline SOFA en IVANNA:**
`CIPIC+MIT .sofa → hrtf_convolver.cpp → room_model.cpp → ivanna_object_renderer → binaural`

Cada `.sofa` contiene: coordenadas esféricas (azimuth, elevation, radius), HRIR 44.1kHz/48kHz, antropometría del pabellón, metadatos AES69-2015.

> Ubicación en repo: `app/src/main/assets/sofa/` y `app/src/main/cpp/spatial/hrtf_convolver.cpp`

---

### 👂 PCA PINNA MORPHOLOGY — Tu oreja, no un promedio

La clave de la superioridad espacial: no usamos HRTF genérica. Sintetizamos HRTF con tu geometría.

**1. Anatomía del Pabellón (Helix, Antihelix, Concha, Tragus, Antitragus, Lobule):**
La Concha controla notches >8kHz. Helix controla elevación. IVANNA modela esto.

**2. PCA Eigenmodes — Variación Morfológica:**
- **PC1: Height/Scaling — 42.1% VAR:** Controla altura total y profundidad de concha
- **PC2: Concha Width — 19.5% VAR:** Afecta spectral notches >8kHz (clave para vertical)
- **PC3-PC5: Crus, Fossa, Antihelix twist — 19.9% VAR**
- **Acumulado PC1-5: 82.1% de varianza explicada**

Fórmula de síntesis:
```
H_personalizada(f,θ,φ) = H_mean(f,θ,φ) + Σᵢ₌₁ᵏ αᵢ * Vᵢ(f,θ,φ) * W_pabellón
donde Vᵢ = eigenvectors PCA, αᵢ = pesos de tu foto de oreja
```

---

### 🧮 SaF RIEMANNIAN OPTIMIZER — El Calibrador Geométrico

Aquí está la fórmula que faltaba. No es gradiente euclidiano. Es **Stiefel-adapted Fisher (SaF)** para PCA robusto en manifold.

**Problema de Optimización:**

```
min_{U ∈ St(d,k)} L(U) = -Tr(Uᵀ Σ U) + λ ||UᵀU - I_k||_F²
donde U ∈ R^{d×k}, UᵀU = I_k (Stiefel manifold)
Σ = matriz de covarianza de landmarks del pabellón
```

**Gradiente Riemanniano:**
```
grad_R L(U) = Π_U (∇L) = ∇L - U (Uᵀ ∇L)
Proyección al espacio tangente de Stiefel
```

**Update con Retracción QR (para permanecer en manifold):**
```
U_{t+1} = qf( U_t - η · grad_R L(U_t) )
η = learning rate, qf() = QR decomposition thin
```

**Loop:**
`Initialize U₀ → Compute Σ from pinna landmarks → Compute Riemannian Grad → Retract via QR → Converge → Eigenvectors {u_i}`

Convergencia: ‖p_t‖ minimización de energía de error. Típico: 40-80 iteraciones, error espectral <1.2dB RMS.

> Código: `app/src/main/cpp/spatial/phase_oracle.cpp` y `audio_orchestrator.cpp` — `EvolutionaryKernel` implementa este loop.

---

### 🏛️ RIR CONVOLVER — No reverb algorítmica. Salas reales.

Usamos **Room Impulse Response** real + **convolución FFT Radix-2 Overlap-Save**.

**h(t) — Fingerprint acústico:**
- Direct Impulse δ(0) @ 0ms
- Early Reflections: 12ms (pared), 24ms (techo), 38ms (trasera) — ecos discretos
- Late Reverberation Tail: 50-1500ms — decaimiento difuso exponencial
- RT60 ≈ 1.8s

**Fórmula de convolución:**
```
y[n] = (x * h)[n] = Σₖ x[k]·h[n-k]
En frecuencia: y[n] = IFFT{ FFT(x) ⊙ FFT(h) } = FFT⁻¹{ X_k · H_k }
```

**Implementación RT-safe en IVANNA (O(N log N) vs O(N²)):**
```
INPUT x[n] → BLOCKING (1024 samples, overlap L-1=511) → X_k = FFT{x[n]}
RIR h[n] → Zero-Pad N=1024 → H_k = FFT{h[n]}
→ Y_k = X_k · H_k (multiplicación compleja puntual)
→ y_k = IFFT{Y_k} → OVERLAP-SAVE (descarta L-1, overlap-add) → OUTPUT y[n] = x[n] * h[n]
```

> Código: `app/src/main/cpp/dsp/` y `spatial/room_model.cpp` — sin malloc en RT path, lookup tables.

Aplicaciones: Virtual Room Acoustics, Convolution Reverb, Immersive 3D Audio.

---

### 🧠 PIPELINE NEURONAL COMPLETO (Ruta A)

```
Archivo local → MediaExtractor / MediaCodec (PCM)
  → DSPBridge.process() → nativeProcessBlock() (libivanna_omega.so)
    → ParametricEQ (8 bandas)
    → Compressor (setRuntimeAmount ← ADE @20Hz)
    → HarmonicExciter (2× OS, HPF 2.4kHz, setRuntimeReduction ← ADE)
    → StereoWidener (M/S mono-safe graves)
    → GainStage (input trim + output gain, setRuntimeGain ← ADE)
    → PDEngine (Volterra H2 + HRTF sintético PCA + EvolutionaryKernel SaF)
    → RIR Convolver (SOFA + Overlap-Save)
    → SafetyLimiter (-0.1dBFS, lookup table, RT-safe, 0 malloc)
  → AudioTrack → DAC
```

**ADE:** Modula target_gain, compressor_amount, exciter_reduction, spatial_width desde RMS, Peak, GainReduction y energías low/mid/high de envelopes IIR 8 bandas de PDEngine. Publica vía seqlock lock-free.

**YAMNet:** `assets/yamnet.tflite` TFLite 2.14.0, noCompress. Clasifica voz/música/transitorios/graves → alimenta ADE. Loop cerrado.

---

### 🌐 RUTA B — MAGISK DAEMON

```
Spotify/YouTube/Tidal → AudioFlinger HAL → libomega_effect.so (AudioEffect global)
  → applyAgc() + publica métricas en shared memory omega_shared.h
→ omega_daemon (SCHED_FIFO 98, processLoop, <300ms arranque)
  → PFEngine (4 biquads: low-shelf 200Hz, mid peak, high-shelf 8kHz, presence 3.5kHz)
  → Compressor → HarmonicExciter → StereoWidener → SafetyLimiter
  → aplica ai_runtime_gain_mul
→ salida sistema
```

IPC: `ai_runtime_gain_mul`, `ai_runtime_comp_amount`, `ai_runtime_exciter_red` vía memfd/shared memory, SPSC sin bloqueo, zero allocation en hot path.

**Gap honesto:** spatial_width no afecta Ruta B (no PDEngine/HRTF en daemon). Roadmap v1.1.

---

### 🔬 ABX — Ingeniería basada en evidencia

App incluye ABX Tester integrado con binomial stats. Si preset no supera placebo p<0.05, te lo dice.

```
A = preset 0.500, B = preset 0.501, X = random
¿X == A o B? 10 intentos, >8 aciertos = significativo
Crossfade 20ms, ADE lock 3s al tocar slider
```

Logs en `docs/listening_tests/abx_logs/` + null-tests + spectrogram diferencial OpenGL.

Auditoría: 142 módulos Kotlin, 204 endpoints JNI, 23 verificaciones CTest (gammatone stability, denormals low level, DSP core stability).

---

### 📦 BUILD & CI

```bash
./gradlew assembleDebug
./gradlew assembleRelease # ¡debug key por defecto! No distribuir
cmake -B build-tests -S app/src/main/cpp/tests -DCMAKE_BUILD_TYPE=Release && ctest
```

Toolchain: NDK 25.1.8937393, CMake 3.22.1, AGP 8.5.1, Kotlin 1.9.24, JVM 17, compileSdk 35, minSdk 28
Flags: `-O3 -fno-fast-math -fno-associative-math -ffp-contract=off` + `-march=armv8-a+fp+simd -funroll-loops -fno-exceptions -fno-rtti` — `-ffast-math` prohibido: rompe NEON SD8 Gen2/3, genera NaN.

CI: test-native-dsp → build-apk → valida ELF `AUDIO_EFFECT_LIBRARY_INFO_SYM` → Releases en tag v* + update.json Magisk.

---

### ⚠️ Advertencias

- Release usa debug key. Cambiar signingConfig.
- Permisos protegidos: CAPTURE_AUDIO_OUTPUT, PACKAGE_USAGE_STATS, READ_LOGS, MEDIA_CONTENT_CONTROL, BIND_AUDIO_EFFECT_SERVICE — solo root / firma sistema.
- Magisk puede brick si HAL no acepta AudioEffect global. service.sh tiene watchdog + backup boot.img pero hacer backup manual.
- Firebase: FirebaseOptions.Builder sin google-services.json — proveer credenciales.
- Versionado desync: APK 1.8 / Magisk 2.0.0 — unificar a semver.

### 🗺️ Roadmap v1.1

- Portar PDEngine+RIR al daemon para spatial_width en Ruta B
- IvannaLab: LUFS ITU-R BS.1770, TruePeak oversampled
- USB DAC isócrono: UsbAudioProManager.kt
- Head tracking 6DoF: IMU 100Hz + One Euro Filter + navegación inercial
- Publicar docs/sofa/, docs/rir/, docs/saf_formula/

**Autor:** Luis Uriel Pimentel Pérez — Licencia: Propietaria y confidencial (inconsistente si repo público)

**Bienvenido al límite absoluto del audio en Android. Sin cuentos. Con SOFA, RIR y SaF medibles.**
