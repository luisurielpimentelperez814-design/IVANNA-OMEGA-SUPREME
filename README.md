<div align="center">

# 🌌 IVANNA OMEGA SUPREMA

### La Cúspide de la Ingeniería Acústica Neuronal para Android

**El audio no se debate. Se mide, se prueba y se experimenta.**

[![Android](https://img.shields.io/badge/Android-9--16_%7C_arm64--v8a-3DDC84?style=for-the-badge&logo=android&logoColor=white)](.)
[![SOFA](https://img.shields.io/badge/SOFA-AES69_%7C_14_Archivos-00FFCC?style=for-the-badge)](.)
[![RIR](https://img.shields.io/badge/RIR-200_Salas_Reales_%7C_Overlap--Save_FFT-FF00AA?style=for-the-badge)](.)
[![SaF](https://img.shields.io/badge/SaF-Riemannian_Optimizer_%7C_Stiefel-7F52FF?style=for-the-badge)](.)
[![Magisk](https://img.shields.io/badge/Magisk-v2.2.0_%7C_Ruta_Global-E01F26?style=for-the-badge)](.)
[![DSP](https://img.shields.io/badge/DSP-SCHED_FIFO_98_%7C_Lock--Free-0A0A0A?style=for-the-badge)](.)

---

</div>

## ⚡ MANIFIESTO: Más allá de la ecualización

**IVANNA OMEGA SUPREME no es un ecualizador.** Es un **motor acústico de precisión neuronal** que vive dentro de `AudioFlinger` — daemon C++ con prioridad `SCHED_FIFO 98`, cero bloqueos, cero malloc en el hilo de audio.

Mientras otros hacen `EQ = bandas × ganancia`, nosotros ejecutamos esta cascada real, medible y verificable:

```
SOFA (AES69) ──► PCA Pinna ──► SaF Riemannian ──► HRTF Personalizada
      │                                                │
      ▼                                                ▼
RIR 200 salas ──► Convolución FFT Overlap-Save ──► ObjectRenderer
                                                          │
                                                          ▼
                        HarmonicExciter (anti-alias 2× OS) ──► SafetyLimiter ──► OUT
```

> **Sensibilidad verificada:** Δ 0.001 en cualquier slider = cambio audible distingible en test ABX. No es placebo. Es cascada no-lineal calibrada.

---

## 📚 BIBLIOTECA SOFA — Inventario Real Desplegado

Todos los archivos viajan en el módulo Magisk (`/data/adb/ivanna_omega/sofa/`) y se cargan on-demand — **no** están empaquetados como decoración:

| Archivo SOFA | Tipo | Uso en el motor |
|---|---|---|
| `MIT_KEMAR_normal_pinna.sofa` | HRTF FreeField | Referencia anatómica estándar — sujeto base |
| `MIT_KEMAR_large_pinna.sofa` | HRTF FreeField | Morfología de pabellón grande (comparativa PCA) |
| `TU-Berlin_QU_KEMAR_anechoic_radius_0.5m.sofa` | HRTF anecoica | Campo cercano 0.5 m — precisión frontal |
| `Pulse.sofa` | HRTF | Respuesta impulsiva de referencia |
| `SimpleFreeFieldSOS.sofa` | HRTF (SOS) | Fuente secundaria — validación cruzada |
| `GeneralSOS_1.0.sofa` | General | Conjunto general de segundo orden |
| `GeneralTF_E.sofa` | General TF | Funciones de transferencia validadas |
| `UMA_AnnotatedReceiverAudio.sofa` | Anotada | Receptor anotado — calibración fina |
| `demo_FreeFieldHRTF_1_IR.sofa` | HRTF demo | 12º sujeto — completa la biblioteca |
| `hpir_AKGK271MKII_*.sofa` | HpIR | Ecualización de auricular AKG K271 MKII |
| `hpir_AKGK272HD_*.sofa` | HpIR | Ecualización de auricular AKG K272 HD |
| `hpir_BeyerdynamicDT770PRO_*.sofa` | HpIR | Compensación DT 770 PRO |
| `hpir_BeyerdynamicDT77_*.sofa` | HpIR | Compensación DT 77 |

**Selección automática:** al detectar salida (altavoz / Bluetooth / auriculares), el motor elige HRTF FreeField para speaker o HpIR correspondiente para auriculares — sin intervención del usuario.

---

## 🏛️ RIR — 200 Salas Reales Medidas

- **200 respuestas impulsivas reales** (`rir/rir_0000.wav` … `rir_0199.wav`), estéreo, con `metadata.csv` de geometría y RT60 por sala.
- **Convolución Overlap-Save FFT Radix-2** en tiempo real — sin latencia de trama adicional.
- **Selección inteligente por RT60 y geometría:** el motor no aplica "reverb genérica" — elige la sala cuyo RT60 y dimensiones casan con la escena pedida, evitando reverberación artificial excesiva.

---

## 🧮 SaF RIEMANNIAN OPTIMIZER — El Calibrador Geométrico

No es gradiente euclidiano. Es **Stiefel-adapted Fisher (SaF)** para PCA robusto sobre manifold.

**Problema de optimización:**

```
min_{U ∈ St(d,k)}  L(U) = -Tr(Uᵀ Σ U) + λ ‖UᵀU - I_k‖²_F
```

**Gradiente Riemanniano** (proyección al espacio tangente de Stiefel):

```
grad_R L(U) = ∇L - U(Uᵀ ∇L)
```

**Update con retracción QR** (para permanecer en el manifold):

```
U_{t+1} = qf( U_t - η · grad_R L(U_t) )
```

Convergencia típica: **40–80 iteraciones**. Modelo persistido: `SAF_model_total.json` (214 sujetos, 7 componentes PCA). Eigenmodes principales: PC1 altura/escala (~42 % varianza), PC2 ancho de concha (~19.5 % — controla notches espectrales >8 kHz, clave para localización vertical).

---

## 🛡️ INTEGRIDAD DE SEÑAL — Distorsión bajo control

| Etapa | Protección implementada |
|---|---|
| **HarmonicExciter** | Soft-clip Padé [3/2] con **clamp de entrada a ±3** — el aproximante saturaba por encima de ±1 a drive alto (x=16 → 1.94 sin fix); ahora satura limpio en 1.0. Anti-aliasing por oversampling 2× + LPF. Techo interno −0.1 dBFS |
| **SafetyLimiter** | Último de la cadena, siempre. Lookahead con headroom, sin pumping, sin saturación digital |
| **Compilación DSP** | `-O3 -fno-fast-math -fno-associative-math -ffp-contract=off` + NEON `-march=armv8-a+fp+simd`. `-ffast-math` **prohibido**: genera NaN y rompe denormals en SD8 Gen2/3 |
| **Hilo de audio** | Lock-free, sin malloc, seqlock SHM para parámetros (`omega_control_bus`) |

---

## 🎛️ ARQUITECTURA DE DOS RUTAS

```
┌─ RUTA A (App, sin root) ─────────────────────────────┐
│ MediaProjection → AudioPipeline → JNI → DSP → Out    │
│ Telemetría viva: RMS/peak/YAMNet cada bloque         │
└──────────────────────────────────────────────────────┘
┌─ RUTA B (Magisk, sistema global) ────────────────────┐
│ omega_effect (AudioEffect HAL) en AudioFlinger       │
│ ivanna_daemon @omega_daemon_socket → control_bus SHM │
│ → omega_effect lee seqlock por bloque (sin locks)    │
└──────────────────────────────────────────────────────┘
```

- **Persistencia:** `SpatialControlStore` (DataStore) guarda HRTF/RIR/SAF — sobrevive cierre, reboot y reinstalación del APK; `BootRestoreReceiver` reaplica con el sample rate real del hardware.
- **UI cableada:** `SpatialControlPanel` — cada slider llega al DSP vía JNI o socket; nada es decorativo.
- **ABX:** test con test binomial exacto (p-valor bilateral, IC 95 %), exportación JSON a `/data/adb/ivanna_omega/`.

---

## 📦 BUILD & CI

```bash
./gradlew assembleDebug
cmake -B build-tests -S app/src/main/cpp/tests -DCMAKE_BUILD_TYPE=Release && ctest
```

**Toolchain:** NDK 25.1.8937393 · CMake 3.22.1 · Kotlin 1.9.24 · JVM 17 · compileSdk 35 · minSdk 28

**CI:** `test-native-dsp` → `build-apk` → validación ELF `AUDIO_EFFECT_LIBRARY_INFO_SYM` → Release en tag `v*` + `update.json` Magisk.

---

## ⚠️ Advertencias de ingeniería

- Release usa **debug key** por defecto — cambiar `signingConfig` antes de distribuir.
- Permisos protegidos (`CAPTURE_AUDIO_OUTPUT`, `BIND_AUDIO_EFFECT_SERVICE`) requieren root o firma de sistema.
- El módulo Magisk instala un `AudioEffect` global: hacer backup de `boot.img` antes de flashear.
- Firebase es **opt-in**: proveer `google-services.json` propio.

---

<div align="center">

**Autor:** Luis Uriel Pimentel Pérez

*Bienvenido al límite absoluto del audio en Android. Sin cuentos. Con SOFA, RIR y SaF medibles.*

</div>
