<div align="center">

<img src="docs/release_media/ivanna_omega_hero.svg" alt="IVANNA OMEGA SUPREME v2.4.1 — Sistema Operativo Neuroacústico de Sistema Completo para Android por Luis Uriel Pimentel Pérez (GORE TNS)" width="100%" />

<br/>

<img src="app/src/main/res/drawable/ic_ivanna_logo.png" alt="IVANNA OMEGA SUPREME Official Emblem" width="122" />

# ⬡ IVANNA OMEGA SUPREME `v2.4.1` ⬡

### Sistema Operativo de Supremacía Neuroacústica, Sincronía A/V Cero-Desfase y Reconstrucción de Realidad Acústica en Lazo Cerrado para Android
**C++20 Nativo (`456` archivos · `174.6K LOC`) · SIMD ARM64 NEON (`float32x4_t`) · Sincronía de Cine y Streaming Cero-Desfase (Amazon Prime Video, Netflix, Disney+) · Lazo Cerrado `OmniHolographicSingularityEngine` · Guardia Matemática $C^2$ Cero-Artefactos (`RationalC2SoftCeiling`) · Atlas de Escena Musical (`IvannaMusicIntelligenceEngine` · 12 Estilos + Pesos Binarios `IVW1` de 8 774 Parámetros) · 5 Ejes Cuántico-Neuromórficos + Inversión Coclear PINN (sample-by-sample, 0 muestras de lookahead) · Entrenamiento Conjunto 255-SOFA + 7D-SAF + 200-RIR desde $t = 0\text{ ms}$ (Root & Sin Root) · Asistente Cognitivo Híbrido con Gemini 2.5 Flash**

*Arquitectura, Investigación, Física Matemática, Diseño de Sistemas y Autoría Principal por*
### **Luis Uriel Pimentel Pérez — GORE TNS**

<br/>

[![Build CI](https://img.shields.io/github/actions/workflow/status/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/build.yml?branch=main&style=for-the-badge&logo=githubactions&logoColor=white&label=IVANNA%20CI%20%26%20RELEASE%20v2.4.1&color=23F09A)](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions/workflows/build.yml)
[![Host CTest Suite](https://img.shields.io/github/actions/workflow/status/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/tests-host.yml?branch=main&style=for-the-badge&logo=cplusplus&logoColor=white&label=CTEST%20174%2F174%20%28ASAN%2BUBSAN%29&color=23F09A)](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions/workflows/tests-host.yml)
[![Static Gates](https://img.shields.io/badge/Puertas%20Est%C3%A1ticas-6%2F6%20PASS%20%2834%2F34%20RT%29-00F0FF?style=for-the-badge&logo=shield&logoColor=white)](scripts/check_rt_safety.py)
[![Supply Chain SLSA](https://img.shields.io/github/actions/workflow/status/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/supply-chain.yml?branch=main&style=for-the-badge&logo=slsa&logoColor=white&label=SLSA%20%C2%B7%20SBOM%20%C2%B7%20COSIGN&color=F7B733)](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions/workflows/supply-chain.yml)

[![Version](https://img.shields.io/badge/Versi%C3%B3n%20Unificada-v2.4.1%20%28241%29-00F0FF?style=for-the-badge&logo=android&logoColor=white)](version.properties)
[![Android SDK](https://img.shields.io/badge/Android-9.0%20%E2%86%92%2015%20%28API%2028%E2%80%9335%29-3DDC84?style=for-the-badge&logo=android&logoColor=white)](app/build.gradle.kts)
[![Native Core](https://img.shields.io/badge/N%C3%BAcleo%20DSP-456%20Archivos%20C%2B%2B20%20%28174.6K%20LOC%29-6FF3FF?style=for-the-badge&logo=cplusplus&logoColor=white)](app/src/main/cpp/)
[![UI Compose](https://img.shields.io/badge/UI%20Aurora%20Obsidiana-221%20Archivos%20Kotlin%20%2852.2K%20LOC%29-A97FFF?style=for-the-badge&logo=kotlin&logoColor=white)](app/src/main/java/com/ivanna/omega/)
[![Atlas IVW1](https://img.shields.io/badge/Atlas%20de%20Escena-12%20Estilos%20%C2%B7%208774%20Pesos%20IVW1-FF2E93?style=for-the-badge)](docs/ATLAS_ESCENA.md)
[![Spatial Datasets](https://img.shields.io/badge/Datasets%20Reales-255%20SOFA%20%C2%B7%207D%20SAF%20%C2%B7%20200%20BRIR%20%C2%B7%2012%20IHR1-38BDF8?style=for-the-badge)](app/src/main/cpp/spatial/SofaSafRirMasterKnowledge.hpp)
[![Author](https://img.shields.io/badge/Autor-Luis%20Uriel%20Pimentel%20P%C3%A9rez%20%28GORE%20TNS%29-FFD166?style=for-the-badge)](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME)

<br/>

> **«No es un ecualizador ni un wrapper sobre efectos de Android. Es un sistema operativo acústico completo escrito en C++20 y ARM64 NEON, con física electrodinámica de transductores, inversión biomecánica coclear, espacialización holográfica entrenada sobre mediciones anatómicas reales, un Atlas de Escena Musical de 12 estilos que reconfigura la sala sin clics, protección matemática $C^2$ contra distorsión y un cerebro neuromórfico en lazo cerrado que escucha cada muestra y decide cómo debe sonar.»**
> — *Luis Uriel Pimentel Pérez (GORE TNS)*

---

### 🧭 Índice de Navegación Rápida

| Bloque Arquitectónico | Secciones Directas |
|:---|:---|
| **I. Fundamentos y Arquitectura Dual** | [**§1. Visión General y Tablero de Métricas Verificadas**](#-1-qué-es-ivanna-omega-supreme-y-tablero-ejecutivo-v240) · [**§2. Fusión Maestra en Lazo Cerrado (`OmniHolographicSingularityEngine`)**](#-2-fusión-maestra-en-lazo-cerrado-omniholographicsingularityengine--declarativeunifiedpipeline) |
| **II. Física Matemática y Atlas v2.4.0** | [**§3. Blindaje C² Cero-Artefactos (`SupremeAcousticStabilityGuard`)**](#-3-blindaje-acústico-cero-artefactos-y-cero-clipping-supremeacousticstabilityguard) · [**§4. Atlas de Escena Musical (`IvannaMusicIntelligenceEngine` + `IVW1`)**](#-4-atlas-de-escena-musical-v240-ivannamusicintelligenceengine-pesos-ivw1-y-motores-físicos-de-sala) |
| **III. Motores Supremos y Espacialización** | [**§5. Los 5 Ejes Supremos + Inversión Coclear PINN**](#-5-los-5-ejes-de-supremacía-cuántico-neuromórfica--inversión-coclear-pinn-0-muestras-de-lookahead) · [**§6. Hipermotor Fases 0–15 y 7 Ejes Espaciales**](#-6-hipermotor-de-reconstrucción-de-realidad-acústica-fases-015-y-los-7-ejes-espaciales) · [**§7. Entrenamiento 255-SOFA + 7D-SAF + 200-RIR**](#-7-entrenamiento-conjunto-sofa-255-aes69--saf-manifold-7-d--rir-200-salas-desde-t--0-ms) |
| **IV. Hardware, IA, Daemon y Verificación** | [**§8. Cadena DSP, DAC USB-C y Bluetooth**](#-8-cadena-dsp-de-9-etapas-ruta-isócrona-usb-c-dac-y-bluetooth-master-path) · [**§9. Cerebros IA, TinyML y Gemini 2.5**](#-9-inteligencia-acústica-tinyml-neuromórfico-y-asistente-cognitivo-gemini-25) · [**§10. Daemon Root, SHM v5 y UI Compose**](#-10-daemon-root-ipc-lock-free-shm-v5-y-ui-aurora-obsidiana) · [**§11. Honestidad Radical de Ingeniería**](#-11-honestidad-radical-de-ingeniería-lo-que-ivanna-hace-y-lo-que-no-hace) · [**§12. Comparativa Industrial**](#-12-ivanna-omega-supreme-frente-a-los-estándares-comerciales-de-la-industria) · [**§13. Matriz CI (`174/174 CTest`) e Instalación**](#-13-matriz-de-verificación-determinista-6-puertas--174174-ctest-e-instalación) |

</div>

---

## ✦ 1. ¿Qué es IVANNA OMEGA SUPREME y Tablero Ejecutivo (`v2.4.1`)?

### 1.1 En palabras que cualquier persona puede entender

Cuando escuchas música, películas o videojuegos en un teléfono Android, el sonido atraviesa varios cuellos de botella: el mezclador del sistema recorta picos digitales, los procesadores comerciales (como Dolby Atmos móvil) comprimen el rango dinámico aplastando los micro-detalles, los audífonos colorean las frecuencias y, cuando activas varios efectos al mismo tiempo, el volumen suele saturarse ("cartonear" o raspar) o producir pequeños chasquidos (*clicks*) al cambiar de modo.

**IVANNA OMEGA SUPREME (`v2.4.1`)** resuelve esto de raíz mediante seis logros de ingeniería verificables línea por línea en el repositorio:

1. **Sincronía A/V Perfecta y Cero Eco en Plataformas de Streaming (`Cinema & Streaming Engine`)**:
   - Resuelve el histórico problema de **Amazon Prime Video, Netflix, Disney+ y YouTube** en Android: elimina el efecto Haas (eco metálico y retardo labial de $+35\text{ ms}$) mediante el **Árbitro Dinámico de Transmisión**. En apps de video se excluye automáticamente la captura por loopback, se mutea la re-inyección redundante por `AudioTrack` y se canaliza el audio exclusivamente por la ruta nativa *in-place* a **0.00 ms de latencia**. El vástago central vocal se bloquea en fase evitando filtrado en peine, el pico directo de las respuestas RIR se alinea a la muestra cero y se atenúan retumbes subsónicos para una inteligibilidad de diálogo prístina.
2. **Cero distorsión en zona lineal, cero saturación y cero chasquidos (`SupremeAcousticStabilityGuard`)**:
   - Actúa como un **protector matemático invisible**: mientras el audio está en un volumen seguro (por debajo del $88\%$ de la escala digital, $|x| \le 0.88\text{ FS}$), **no altera ni un solo bit** ($\text{THD} = 0.00000\%$, $\text{SNR de cadena} = 132.39\text{ dB}$). Si un pico repentino amenaza con saturar tus audífonos o bocinas, aplica un techo racional de curvatura continua ($C^2$) que amortigua el pico con suavidad analítica hacia la asíntota $0.994\text{ FS}$ sin aplastar la música.
   - Cuando enciendes o apagas cualquier motor, el gobernador térmico oscila o cambias de sala, el sistema "cose" la onda de sonido muestra a muestra (`HermiteC1BoundaryStitcher` + `SupremeTransitionEnvelope`) para que jamás escuches un *click*, *pop* o salto brusco.
3. **Atlas de Escena Musical Automático con Cero Intervención Manual (`IvannaMusicIntelligenceEngine` + `IVW1`)**:
   - El clasificador neuronal carga en arranque los **8 774 parámetros pre-entrenados (`ivanna_weights.ivw1`)**, extrae 12 descriptores psicoacústicos por ventana y evalúa la verosimilitud sobre **12 estilos musicales maestros** (desde *Progressive Rock 70s* y *Symphonic Hall* hasta *Hip-Hop Sub-808* y *Podcast Voice*). En menos de $1.5\text{ segundos}$ identifica el carácter de la pista y reconfigura mediante un triple-buffer atómico (`SceneTargetBus`) la de-reverberación estadística de cola, las reflexiones tempranas físicas de 6 paredes y la riqueza armónica Chebyshev $T_2/T_3/T_4$.
4. **Todos los "cerebros" del motor trabajan unidos en un solo lazo cerrado (`OmniHolographicSingularityEngine`)**:
   - En lugar de tener efectos aislados que compiten entre sí y suman volumen a ciegas, la inteligencia artificial analiza la música en segundo plano (`HeavyWorkerEngine`, sin retrasar el audio ni bloquear el procesador) y guía instantáneamente al motor espacial: si las guitarras tapan la voz del cantante, abre espacio hacia los lados para los instrumentos y despeja la voz en el centro manteniendo exactamente la misma energía total ($\sqrt{M^2 + S^2}$ invariante).
5. **Acústica medida en el mundo real desde el primer milisegundo ($t = 0\text{ ms}$)**:
   - No usa efectos de eco genéricos. Viene entrenado con **255 mediciones anatómicas de oído humano (SOFA AES69-2015)**, un **modelo matemático de 7 dimensiones de pabellón auricular (SAF de 214 sujetos)**, **12 bancos HRTF `.ihr1` de 128 taps** y **200 salas acústicas reales grabadas (BRIR)**, arrancando calibrado desde el instante en que abres la aplicación.
6. **Funciona con Root (en todo el sistema) y Sin Root (en la app, por captura y por DAC USB-C directo)**:
   - Si tienes **Magisk o KernelSU (Ruta B)**, se instala dentro del servidor de audio nativo de Android (`audioserver`) para procesar todas las aplicaciones del teléfono con prioridad `SCHED_FIFO 98`.
   - Si **no tienes Root (Ruta A)**, ejecuta exactamente el mismo motor C++20, el mismo Atlas de Escena, los mismos 5 Ejes Supremos y las mismas 200 salas reales desde la aplicación, mediante captura `MediaProjection` o enviando audio puro directamente a tu **DAC USB-C** por transferencia isócrona de hardware (`usbfs`).

---

### 1.2 Tablero Ejecutivo de Métricas Reales del Repositorio (`v2.4.1`)

| Dimensión Auditada | Cifra Exacta en Código / CI | Evidencia Directa Verificable |
|:---|:---:|:---|
| **Versión Unificada (`Unified Version Manager`)** | **`v2.4.1` (`versionCode = 241`)** | `version.properties` $\leftrightarrow$ `magisk_module/module.prop` $\leftrightarrow$ `magisk_module/update.json` |
| **Código Fuente Nativo C/C++20 (`app/src/main/cpp/`)** | **`456` archivos (`174 633 LOC`)** | `96` unidades de traducción compiladas en `CMakeLists.txt` + `147` headers de producción (0 huérfanos) |
| **Aplicación e Instrumentación Kotlin (`app/src/main/java/`)** | **`221` archivos (`52 172 LOC`)** | Sistema visual **Aurora Obsidiana** en Jetpack Compose + `OmegaEngineBridge` |
| **Puente JNI Biyectivo (`[0.1] check_jni_wiring.py`)** | **`321 de 321` símbolos (`100%`)** | `321` declaraciones `external fun` Kotlin $\leftrightarrow$ `321` funciones `Java_*` C++ + `232 de 232` wrappers invocados |
| **Seguridad de Tiempo Real (`[A3] check_rt_safety.py`)** | **`34/34` entradas RT (`0` violaciones)** | Auditoría estática AST de 2 niveles + intercepción dinámica de `malloc`/`new` en `test_rt_no_alloc.cpp` |
| **Pesos Pre-Entrenados TinyML (`IVW1` Binary Format)** | **`8 774` parámetros (`35 108 bytes`)** | `app/src/main/assets/models/ivanna_weights.ivw1` cargado por `IvannaAudioClassifier::loadWeights` |
| **Suite de Pruebas Nativas Host (`CTest` + `ASan/UBSan`)** | **`174/174` tests en verde (`16.48 s`)** | `telemetry/ctest_summary.json` · `53` ejecutables de prueba C++20 auditados en cada push |
| **Sincronía A/V y Supresión de Eco en Streaming** | **`0.00 ms` desfase relativo · `0.0 dB` eco** | `PlaybackCaptureService` (auto-exclude UIDs + Track Mute) + `IvannaGlobalEffectManager` |

---

### 1.3 Arquitectura Dual de Sistema Completo (Ruta A Sin Root $\leftrightarrow$ Ruta B Con Root)

```mermaid
flowchart TB
    subgraph SOURCES["Fuentes de Audio en el Dispositivo Android"]
        S1["Spotify · YouTube · Juegos · Llamadas · Sistema"]
        S2["Reproductor Interno Hi-Res + MediaProjection (Ruta A Sin Root)"]
    end

    subgraph ROUTEB["RUTA B · System-Wide en audioserver (Magisk / KernelSU)"]
        AF["AudioFlinger GlobalEffect · libomega_effect.so"]
        DAEMON["ivanna_daemon (Root · SCHED_FIFO 98 · PIE + RELRO + BIND_NOW)"]
        SHM["OmegaControlBus SHM Seqlock v5 + /ivanna_supreme_shm_v2 (CAS)"]
        DAEMON <-->|"mmap + CRC32 + Seqlock v5"| SHM
        SHM <-->|"Lectura wait-free por bloque"| AF
    end

    subgraph ROUTEA["RUTA A · En Proceso JNI Lock-Free (Sin Root / Híbrido)"]
        JNI["libivanna_omega.so (JNI C++20 · ARM64 NEON)"]
        DAEMON <-->|"@omega_daemon_socket (Unix) / Fallback TCP 127.0.0.1:12121"| JNI
    end

    subgraph CORE["Núcleo Unificado C++20 por Sesión (0 malloc · 0 locks en Hot-Path)"]
        FUSION["IvannaFusionEngine + Cadena DSP 9 Etapas + 7 Ejes Espaciales"]
        ATLASDSP["Motores Físicos del Atlas: LateReverbSuppressor (185Hz) + PhysicalEarlyReflections (6 paredes) + Chebyshev T2/T3/T4"]
        AXES["5 Ejes Cuántico-Neuromórficos + CochlearActiveInverseEngine (PINN)"]
        UNIFIED["DeclarativeUnifiedPipeline (5 Etapas) + OmniHolographicSingularityEngine"]
        WORKER["HeavyWorkerEngine + IvannaMusicIntelligenceEngine (IVW1 8774 params · 12 Estilos) ⟷ AcousticRealityOrchestrator"]
        GUARD["SupremeAcousticStabilityGuard (DC 5Hz · IsometricGovernor · Hermite C¹ · Rational C²)"]

        FUSION --> ATLASDSP --> AXES --> UNIFIED --> GUARD
        UNIFIED -.->|"tryEnqueueAudio (Wait-Free SPSC)"| WORKER
        WORKER -.->|"SceneTargetBus (Triple-Buffer CAS) + SingularityFieldDescriptor"| ATLASDSP
        WORKER -.->|"SingularityFieldDescriptor (Atomic Release/Acquire)"| UNIFIED
    end

    subgraph OUTPUTS["Salidas Físicas Auto-Calibradas desde t = 0 ms"]
        OUT1["Audífonos AUX 3.5mm / Balanceado · Sala Maestra #51 (RT60 = 0.340 s)"]
        OUT2["DAC USB-C UAC1/UAC2 Isócrono Directo (usbfs 8 URBs en vuelo · 44.1k–384k)"]
        OUT3["Bluetooth A2DP / LDAC / BLE · Sala Maestra #63 Anti-Codec (RT60 = 0.293 s)"]
        OUT4["Altavoz Integrado · Sala Maestra #81 Open Projection + XTC Transaural"]
    end

    S1 --> AF --> FUSION
    S2 --> JNI --> FUSION
    GUARD --> OUT1 & OUT2 & OUT3 & OUT4
```

### 1.4 Galería de Instrumentación Real (`Aurora Obsidiana` UI)

<div align="center">
<table>
  <tr>
    <td align="center" width="25%">
      <img src="docs/release_media/ivanna_home.png" alt="Panel Principal IVANNA OMEGA SUPREME" width="100%"/><br/>
      <sub><b>Consola Maestra Aurora Obsidiana</b><br/>Control en tiempo real y estado del motor</sub>
    </td>
    <td align="center" width="25%">
      <img src="docs/release_media/audio_profiles.png" alt="Perfiles Acústicos y Calibración" width="100%"/><br/>
      <sub><b>Perfiles de Producción y Transductor</b><br/>Conmutación sin clics con persistencia atómica</sub>
    </td>
    <td align="center" width="25%">
      <img src="docs/release_media/rir_panel.png" alt="Panel SOFA · SAF · RIR 200 Salas" width="100%"/><br/>
      <sub><b>Laboratorio Espacial SOFA · SAF · RIR</b><br/>200 salas BRIR reales y manifold 7-D</sub>
    </td>
    <td align="center" width="25%">
      <img src="docs/release_media/telemetria_nael.png" alt="Telemetría Neuroacústica y LAB" width="100%"/><br/>
      <sub><b>Telemetría IAEL &amp; Ivanna LAB</b><br/>Auditoría THD+N, LUFS BS.1770-4 y CPU</sub>
    </td>
  </tr>
</table>
</div>

---

### 1.5 Arquitectura de Sincronía A/V y Cero-Eco para Cine & Streaming (Amazon Prime Video, Netflix, Disney+)

<div align="center">
<img src="docs/release_media/ivanna_cinema_zero_delay_architecture.svg" alt="Arquitectura de Sincronía A/V y Cero-Eco para Cine y Streaming" width="100%" />
</div>

#### El Problema Acústico Real en Dispositivos Android Convencionales
Al reproducir películas en plataformas como **Amazon Prime Video**, **Netflix** o **Disney+**, las herramientas tradicionales de audio sufren de dos fallos acústicos críticos:
1. **El Efecto Haas por Doble Reproducción (+35 ms)**: Android entrega el audio del reproductor de video directamente al hardware a $0\text{ ms}$. Si una suite de efectos captura el audio por `AudioPlaybackCapture` y reinyecta el resultado procesado mediante un `AudioTrack` secundario, el oyente escucha **ambas señales superpuestas con un retardo de entre 25 y 45 milisegundos**. Físicamente esto genera un **filtrado en peine severo** (*comb filtering*) y un **eco metálico slapback** que desincroniza el movimiento de los labios de los actores con su voz.
2. **Eco Artificial en Diálogo por Sala Convolutiva**: Las respuestas de impulso de sala (RIR) y los efectos de reverberación convencionales aplican reflexiones tempranas y colas densas sobre el canal central de voz, haciendo que las conversaciones en una habitación pequeña suenen como si estuvieran dentro de una catedral o un baño vacío.

#### La Solución Determinista en IVANNA OMEGA SUPREME `v2.4.1`

| Vector de Corrección | Implementación en Código C++20 / Kotlin | Resultado Acústico Verificable |
|:---|:---|:---|
| **1. Exclusión Dinámica de Streaming** | `PlaybackCaptureService.kt` escanea e invoca `captureBuilder.excludeUid()` sobre todos los paquetes de video (`amazon`, `prime`, `netflix`, `disney`, `hbo`, `youtube`). | La captura por loopback jamás intercepta el flujo de cine, evitando duplicación de audio desde el kernel de Android. |
| **2. Silenciamiento de Re-inyección Loopback** | En `PlaybackCaptureService.kt`, si se detecta sesión de video o modo cine activo, se ejecuta `audioTrack?.setVolume(0.0f)`. | Cero sonido duplicado en bocinas o audífonos. Telemetría (Bark64, espectro, NPE) sigue $100\%$ activa sin tocar el transductor. |
| **3. Bloqueo de Fase en Diálogo Central** | `IvannaAudioPipeline.hpp` (`lateralObjPtrs[0] = nullptr;`) procesa el canal central ($L+R$) exclusivamente por vía directa seca. | El diálogo no atraviesa las líneas de retardo interaural de Woodworth ITD: **0.00 ms de desfase de fase y cero cancelaciones**. |
| **4. Alineación TOF del Pico Directo RIR** | `RirConvolver.cpp` detecta el pico $n_{\text{peak}}$ de la RIR y lo alinea rígidamente a la muestra $0$ ($t = 0\text{ ms}$). | Elimina el retardo de tiempo de vuelo parásito ($15\text{ a }35\text{ ms}$) que causaba doble impacto transitorio. |
| **5. Ecualización de Inteligibilidad y Bypass de Reverb** | `IvannaGlobalEffectManager.kt` fuerza `reverb.enabled = false`, `virtualizer.enabled = false` y aplica realce vocal ($+2.5\text{ dB}$ en $1\text{k}–4\text{kHz}$, $-2.0\text{ dB}$ en subgraves). | Diálogo cinematográfico nítido y enfocado al frente sin resonancias parásitas ni retumbes destructivos. |

---

## ✦ 2. Fusión Maestra en Lazo Cerrado: `OmniHolographicSingularityEngine` & `DeclarativeUnifiedPipeline`

> **Archivos fuente:** `app/src/main/cpp/include/omega_unified_dsp_stage.hpp` · `app/src/main/cpp/include/omega_wave_stages.hpp` · `app/src/main/cpp/include/acoustic_reality_hyperengine.hpp`

<div align="center">
<img src="docs/release_media/ivanna_singularity_architecture.svg" alt="Arquitectura de Lazo Cerrado Omni-Holographic Singularity Engine, Atlas de Escena y SupremeAcousticStabilityGuard" width="100%" />
</div>

En `v2.4.0`, todas las familias de procesamiento avanzado (Anti-Dolby clásico, inferencia TinyML neuromórfica, separación de fuentes Wave-U-Net, linealización cuántico-física PINN y el hiper-orquestador de realidad acústica) convergen en el **`DeclarativeUnifiedPipeline`** de 5 etapas bajo el contrato estricto `UnifiedDspStage`:

### 2.1 Las 5 Etapas del `DeclarativeUnifiedPipeline`

| Etapa | Clase (`ivanna::unified`) | Bit de Flag (`OmegaDspState`) | Qué ejecuta en el Hot-Path RT (`SCHED_FIFO`) | Acoplamiento Fuera del Hilo RT (`HeavyWorkerEngine`) |
|:-----:|---------------------------|-------------------------------|----------------------------------------------|------------------------------------------------------|
| **0** | `AntiDolbyClassicStage` | `OMEGA_STAGE_ANTIDOLBY_CLASSIC` (`1 << 0`) | Descompresión dinámica multibanda, restauración de factor de cresta y realce armónico suave. **Autoridad única:** cuando este bit está activo en el snapshot, `omega_effect.cpp` desactiva automáticamente la llamada legacy a `ctx->antiDolby` para impedir doble procesamiento. | Sincroniza telemetría de envolvente con el bus de control. |
| **1** | `TinyMLNeuromorphicStage` | `OMEGA_STAGE_TINYML_NEUROMORPHIC` (`1 << 1`) | Encola bloques sub-muestreados y bandas Mel de forma wait-free (`tryEnqueueAudio`) y aplica modulación adaptativa por presencia de voz, música, graves y densidad de spikes LIF. | Ejecuta en hilo worker `TinyMLAudioEngine`, `AntiDolbyAI` (`pi_lstm_milenio.hpp`), **`IvannaNeuromorphicTinyML`** (`processAudioFrame` + `getLatestEmbedding` vía SeqLock) y `LIFPool32` / `LIFPool128`. |
| **2** | `WaveUNetSourceSeparationStage` | `OMEGA_STAGE_WAVE_UNET` (`1 << 2`) | Aplica máscaras de separación de fuentes en tiempo real (aislamiento vocal en canal Mid y expansión de acompañamiento en canal Side) con preservación de fase. | Alimentado por `AutonomousBrain` y `Synthesizer` en `HeavyWorkerEngine`. |
| **3** | `QuantumPINNTransducerStage` | `OMEGA_STAGE_QUANTUM_PINN` (`1 << 3`) | Corrección magneto-elástica de transductor y compensación no-lineal guiada por el manifold neuro-coclear (`NeuroCochlearManifold`) y proyección SAF. | Acoplado a `SaFOptimizer` y descriptores de energía de transductor. |
| **4** | **`OmniHolographicSingularityEngine`** | `OMEGA_STAGE_SINGULARITY_ENGINE` (`1 << 4`) | **Síntesis Holográfica en Lazo Cerrado:** enfoque de fase transitoria all-pass de ganancia unitaria, des-enmascaramiento espectral ortogonal Mid/Side con invariante energético y micro-paralaje fraccional de Farrow de 3.er orden. | `HeavyWorkerEngine::processPacket` invoca `AcousticRealityOrchestrator::orchestrateCycle` (Fases 0–15) y publica atómicamente `SingularityFieldDescriptor`. |

### 2.2 Física y Matemática del `OmniHolographicSingularityEngine` (Etapa 4)

1. **Enfoque de Fase Transitoria por Filtro All-Pass Dispersivo de 1.er Orden (Ganancia Unitaria Exacta)**:
   - Los ataques percusivos sufren dispersión de retardo de grupo en grabaciones comprimidas y filtros de cruce. El motor aplica una red all-pass de primer orden modulada suavemente por el coeficiente `transientFocusCoeff` ($a \in [-0.65, -0.15]$):
     $$y_{\text{ap}}[n] = a \cdot x[n] + x[n-1] - a \cdot y_{\text{ap}}[n-1], \qquad \left|H\left(e^{j\omega}\right)\right| \equiv 1.0 \quad \forall \omega \in [0, \pi]$$
   - Al tener magnitud idénticamente unitaria en todas las frecuencias, **jamás colorea el timbre ni infla el pico de magnitud**, alineando únicamente la coherencia de fase de los transientes.

2. **Des-enmascaramiento Espectral Ortogonal Mid/Side con Conservación Algebraica de Energía**:
   - Guiado por `midVocalPresence` ($g_M$) y `sideHoloAir` ($g_S$) publicados por el orquestador cognitivo, transforma el par estéreo $(L, R) \mapsto (M, S)$ y aplica una rotación renormalizada que conserva estrictamente la potencia instantánea del par:
     $$E_{\text{in}} = M^2 + S^2, \qquad \tilde{M} = g_M M, \quad \tilde{S} = g_S S, \qquad \gamma = \sqrt{\frac{M^2 + S^2}{\tilde{M}^2 + \tilde{S}^2 + \epsilon}}$$
     $$M_{\text{out}} = \gamma \tilde{M}, \qquad S_{\text{out}} = \gamma \tilde{S} \implies M_{\text{out}}^2 + S_{\text{out}}^2 \equiv M^2 + S^2$$
   - De este modo, el motor despeja el centro vocal cuando hay conflicto de enmascaramiento o expande el campo holográfico lateral en pasajes instrumentales **con ganancia neta de potencia exactamente igual a $0.00\text{ dB}$**.

3. **Micro-Paralaje Binaural de Profundidad por Interpolación Fraccional de Lagrange/Farrow de 3.er Orden**:
   - Inyecta una componente de paralaje interaural de sub-muestra ($d \in [0.15, 2.85]\text{ muestras}$) ortogonal entre los canales izquierdo y derecho mediante polinomios cúbicos de Lagrange libres de bifurcaciones:
     $$c_0 = -\frac{\delta(\delta-1)(\delta-2)}{6}, \quad c_1 = \frac{(\delta+1)(\delta-1)(\delta-2)}{2}, \quad c_2 = -\frac{(\delta+1)\delta(\delta-2)}{2}, \quad c_3 = \frac{(\delta+1)\delta(\delta-1)}{6}$$

---

## ✦ 3. Blindaje Acústico Cero-Artefactos y Cero-Clipping: `SupremeAcousticStabilityGuard`

> **Archivos fuente:** `app/src/main/cpp/supreme/SupremeAcousticStabilityGuard.hpp` · `app/src/main/cpp/supreme/SupremeAcousticContinuity.hpp` · `app/src/main/cpp/supreme/SupremeTransitionEnvelope.hpp`

<div align="center">
<img src="docs/release_media/ivanna_c2_stability_and_atlas.svg" alt="Curva Matemática Rational C² Soft Ceiling y Atlas de Escena Musical de 12 Estilos" width="100%" />
</div>

Un problema clásico de los motores de audio multicapa es que cuando el usuario activa simultáneamente ecualización, restauración armónica, espacialización y descompresión, las ganancias se apilan produciendo saturación digital o artefactos de transición (*clicks*). En **IVANNA OMEGA SUPREME**, tanto la **Ruta A (`ivanna_omega_jni.cpp`)** como la **Ruta B (`omega_effect.cpp`)** están protegidas por `SupremeAcousticStabilityGuard`:

### 3.1 Techo Racional Continuo $C^2$ (`RationalC2SoftCeiling`) — Cero Aliasing Armónico

Los limitadores convencionales basados en `std::tanh(x)` distorsionan toda la región lineal desde las muestras más pequeñas, mientras que `std::clamp(x, -1, 1)` tiene discontinuidad en su primera derivada ($C^0$), generando una cascada infinita de armónicos impares y aliasing digital. `RationalC2SoftCeiling` divide el espacio dinámico en dos regiones con continuidad analítica hasta la segunda derivada ($C^2$):

- **Región Lineal Bit-Exacta ($|x| \le x_k = 0.88\text{ FS}$)**:
  $$f(x) = x \qquad \Longrightarrow \qquad \text{THD} = 0.00000\% \quad (\text{identidad IEEE-754 bit a bit, SNR medido} = 132.39\text{ dB})$$
- **Región de Rodilla Racional $C^2$ ($|x| > x_k = 0.88\text{ FS}$, asíntota $L = 0.994\text{ FS}$)**:
  Sea $u = \frac{|x| - x_k}{L - x_k} > 0$. La función de compresión racional es:
  $$f(x) = \mathrm{sgn}(x) \cdot \left[ x_k + (L - x_k) \cdot \frac{u + u^2}{1 + u + u^2} \right]$$
  Propiedades matemáticas verificadas en `Phase7_ZeroArtifactZeroClipTHDAndEnergyConservationAudit`:
  - $f(x_k) = x_k$ (continuidad de posición $C^0$),
  - $f'(x_k) = 1.0$ (continuidad de pendiente $C^1$ con la región identidad),
  - $f''(x_k) = 0.0$ (continuidad de curvatura $C^2$: inflexión suave sin generación abrupta de armónicos de orden superior),
  - $\lim_{|x|\to\infty} |f(x)| = L = 0.994 < 1.0\text{ FS}$ (imposibilidad matemática de clipping en el DAC).

### 3.2 Costura Polinomial Cúbica de Hermite $C^1$ (`HermiteC1BoundaryStitcher` & `ClickFreeStageBase`)

Cada etapa del pipeline unificado hereda de `ClickFreeStageBase`, que incorpora dos defensas deterministas contra *clicks* y *pops*:

- **Empalme de Frontera Hermite $C^1$ (16 muestras $\approx 0.33\text{ ms}$ @ $48\text{ kHz}$)**:
  Cuando una etapa se activa, descongela o cambia de estado (`notifyTransition(lastDry, prevSlope)`), el motor interpola entre la última muestra/pendiente válida $(y_0, \dot{y}_0)$ y la nueva trayectoria procesada usando la base cúbica de Hermite $h_{00}(t) = 2t^3 - 3t^2 + 1$ y $h_{10}(t) = t^3 - 2t^2 + t$:
  $$y_{\text{stitched}}[k] = h_{00}(t_k)\cdot \Delta y_0 + h_{10}(t_k)\cdot N \cdot \Delta \dot{y}_0 + y_{\text{wet}}[k], \qquad t_k = \frac{k}{N}$$
- **Crossfade Híbrido Adaptativo Coherente / Potencia Constante**:
  Calcula en tiempo real el coseno de coherencia inter-ruta entre la señal seca y la señal húmeda del bloque. Si ambas señales están en fase (p. ej., etapas de dinámica o análisis), utiliza una envolvente convexa $C^1$ ($g_{\text{wet}} = 3t^2 - 2t^3, \; g_{\text{dry}} = 1 - g_{\text{wet}}$) que garantiza **identidad bit-exacta cuando $\text{wet} \equiv \text{dry}$**. Si existe rotación de fase (p. ej., convolución espacial o all-pass), interpola suavemente hacia la ley trigonométrica de potencia constante ($\cos(\frac{\pi}{2}t), \sin(\frac{\pi}{2}t)$) evitando caídas de nivel a mitad de transición.

### 3.3 Gobernador Isométrico de Energía (`IsometricEnergyGovernor`) y Bloqueador Sub-Sónico DC

- **`IsometricEnergyGovernor`**: Mide la energía cuadrática media de entrada $E_{\text{in}} = \sum (L_{\text{in}}^2 + R_{\text{in}}^2)$ antes de la cadena de etapas avanzadas y la compara con la energía de salida $E_{\text{out}}$ al terminar la cadena. Si el apilamiento de múltiples etapas activas eleva la energía por encima de un margen de histéresis de $+1.2\text{ dB}$ ($\text{ratio} = 1.318$), aplica un factor de escala $\sqrt{E_{\text{in}} \cdot 1.318 / E_{\text{out}}}$ suavizado muestra a muestra con un filtro exponencial de un polo.
- **`BiquadDcBlocker`**: Filtro paso-alto de primer orden a $5.0\text{ Hz}$ con acumuladores de estado en **doble precisión (`double`)** ($R = 1 - \frac{2\pi \cdot 5}{f_s}$), eliminando cualquier desplazamiento DC asimétrico antes del techo racional $C^2$.

---

## ✦ 4. Atlas de Escena Musical (`v2.4.0`): `IvannaMusicIntelligenceEngine`, Pesos `IVW1` y Motores Físicos de Sala

> **Archivos fuente:** `docs/ATLAS_ESCENA.md` · `app/src/main/cpp/music_intelligence/IvannaMusicIntelligenceEngine.hpp` · `app/src/main/cpp/music_intelligence/StyleBlender.hpp` · `app/src/main/cpp/spatial/SceneTargetBus.hpp` · `app/src/main/cpp/spatial/LateReverbSuppressor.hpp` · `app/src/main/cpp/spatial/PhysicalEarlyReflections.hpp` · `app/src/main/cpp/IvannaAudioClassifier.cpp`

Incorporado y certificado en **`v2.4.0`**, el **Atlas de Escena Musical** dota a IVANNA de percepción estilística continua sin intervención manual del usuario:

### 4.1 Clasificador `IVW1` (`8 774` Parámetros) + `StyleBlender` de 12 Centroides

1. **Formato Binario `IVW1` (`app/src/main/assets/models/ivanna_weights.ivw1`)**:
   - Cabecera verificada de 12 bytes (`magic = "IVW1"`, `version = 1`, `num_params = 8774`) seguida de `8 774` pesos `float32` entrenados mediante Expectation-Maximization reproducible (`tools/train_atlas_em.py`).
   - `IvannaAudioClassifier::loadWeights(const char* path)` valida la firma mágica, versión, conteo exacto de parámetros, ausencia de `NaN`/`Inf` y cuantiza los pesos en el banco interno alineado a 64 bytes (`m_qWeights`) sin reservar memoria dinámica.
2. **Inferencia Gaussiana Diagonal en 12 Dimensiones (`StyleBlender.hpp`)**:
   - Evalúa en cada ventana la distancia de Mahalanobis diagonal frente a los **12 estilos maestros** del Atlas (`progressive_rock_70s`, `analog_warm_60s`, `neo_soul_rnb`, `electronic_idm`, `classical_symphonic`, `jazz_acoustic_quartet`, `hiphop_sub808`, `metal_high_gain`, `ambient_cinematic`, `pop_modern_master`, `latin_percussive`, `podcast_voice_broadcast`):
     $$\ell_k(\mathbf{z}) = -\frac{1}{2}\sum_{d=0}^{11} \left(\frac{z_d - \mu_{k,d}}{\sigma_{k,d}}\right)^2, \qquad p_k = \frac{\exp(\ell_k / T)}{\sum_{j=0}^{11} \exp(\ell_j / T)} \quad (T = 0.55)$$
   - Suaviza el vector de probabilidades con un filtro EMA ($\tau = 2.0\text{ s}$ en estado estacionario, adquisición rápida en arranque en frío $< 1.5\text{ s}$) y pondera la mezcla contra el arquetipo neutro mediante una **compuerta de confianza por entropía de Shannon normalizada**:
     $$c = 1 + \frac{1}{\ln(12)}\sum_{k=0}^{11} \bar{p}_k \ln(\bar{p}_k) \in [0, 1], \qquad g = \mathrm{clamp}\left(\frac{c - 0.25}{0.55 - 0.25},\; 0,\; 1\right)$$

### 4.2 Triple-Buffer Lock-Free `SceneTargetBus` y Motores Físicos Guiados por Escena

| Componente C++20 | Archivo Fuente | Garantía Matemática y Física en el Hilo RT |
|:---|:---|:---|
| **`SceneTargetBus`** | `spatial/SceneTargetBus.hpp` | Triple-buffer lock-free alineado a línea de caché (`alignas(64)`) gobernado por una **única palabra de estado atómica de 1 byte** (`bits [1:0]=writeIdx`, `bits [3:2]=cleanIdx`, `bits [5:4]=readIdx`, `bit 6=dirty`) actualizada vía `compare_exchange_weak` (`acq_rel`/`acquire`). Garantiza **0 lecturas desgarradas (*torn reads*)** bajo contención extrema de hilos. |
| **`LateReverbSuppressor`** | `spatial/LateReverbSuppressor.hpp` | De-reverberador estadístico de cola con **crossover de 3 polos en cascada a $185\text{ Hz}$** (`lp1_`, `lp2_`, `lp3_`): protege íntegramente los subgraves de $60\text{ Hz}$ ($\Delta < 0.3\text{ dB}$) mientras atenúa colas reverberantes exponenciales en $\ge 4.0\text{ dB}$ mediante estimación dual de envolvente rápida/lenta ($\tau_f = 8\text{ ms}$, $\tau_s = 140\text{ ms}$) con retardo decorrelado de $50\text{ ms}$. |
| **`PhysicalEarlyReflections`** | `spatial/PhysicalEarlyReflections.hpp` | Modelo geométrico de **fuente-imagen de 1.er orden para recinto rectangular (shoebox) de 6 superficies** (*pared izquierda, pared derecha, frente, fondo, suelo, techo*). Calcula retardos exactos de propagación $\tau_w = (\ell_w - d_0)/c$, ganancia por ley $1/r$, filtro de absorción aérea/material de 1 polo e **ITD esférico de Woodworth** por reflexión. |
| **`HarmonicRichnessController`** | `music_intelligence/HarmonicRichnessController.hpp` | Síntesis armónica controlada mediante **polinomios ortogonales de Chebyshev** de 2.º, 3.º y 4.º orden ($T_2(x) = 2x^2 - 1$, $T_3(x) = 4x^3 - 3x$, $T_4(x) = 8x^4 - 8x^2 + 1$) con sustracción analítica de offset par y filtro bloqueador DC de 1.er orden a $5\text{ Hz}$. |

---

## ✦ 5. Los 5 Ejes de Supremacía Cuántico-Neuromórfica + Inversión Coclear PINN (0 muestras de lookahead)

> **Archivos fuente:** `app/src/main/cpp/supreme/` · `app/src/main/cpp/neuromorphic/CochlearActiveInverseModel.hpp`

Ejecutados íntegramente en el hilo de tiempo real **muestra a muestra (0 muestras de retardo de bloque añadido, medido por `PerfAuditor::measureAlgorithmicLatencyMs`)**, **cero asignaciones de memoria dinámica (`malloc`/`new`)**, protección hardware contra subnormales (`ScopedFpDenormalsToZero`: `FPCR.FZ=1` en ARM64 / `MXCSR` en x86_64) y continuidad de estado gestionada por `SupremeStateContinuityManager`:

| Eje | Módulo C++20 (`app/src/main/cpp/supreme/`) | Física, Matemática y Vectorización SIMD ARM NEON |
|:---:|--------------------------------------------|--------------------------------------------------|
| **Eje 1** | `WarpedLatticeTransducerInverter.hpp`<br/>*(Anti-Dirac + De-Clipper Hermite)* | • **De-Clipper Cúbico de Hermite**: detecta mesetas recortadas ($|x| > 0.95, |\Delta x| < 10^{-5}$) y reconstruye la cresta perdida por interpolación polinómica cúbica.<br/>• **Celosía de Fase Mínima Deformada en Escala Bark** ($\lambda \approx 0.72$ @ $48\text{ kHz}$) con arquetipos específicos por ruta (`#0` AUX/USB, `#1` Bluetooth, `#2` Altavoz).<br/>• **Linealización Electrodinámica Lorentz $Bl(x)$** de bobina móvil + adaptación NLMS restringida al hipercubo de estabilidad de Schur ($|\kappa_m| < 0.95$) con sonda Micro-Chirp enmascarada ($-100\text{ dBFS}$). |
| **Eje 2** | `PhaseCoherentTransharmonicSynthesizer.hpp`<br/>*(Anti-DSEE + Cinta 2" Jiles-Atherton)* | • **Red Neuronal en Dominio Complejo (CVNN)** con activación `modReLU` equivariante de fase: restaura armónicos superiores preservando la derivada de fase instantánea.<br/>• **8 Osciladores DDSP en Cuadratura** vectorizados en registros `float32x4_t` ARM NEON.<br/>• **Cancelación Activa Cuadrática de Distorsión por Intermodulación (IMD)** + modelo físico de histéresis magnética de **Jiles-Atherton** ($M = M_s \mathcal{L}(H_e/a)$) de cinta analógica de 2 pulgadas a 30 IPS. |
| **Eje 3** | `SnnNmfHoaUpmixer.hpp`<br/>*(Anti-Dolby Neuromórfico)* | • **Red Neuronal de Impulsos (SNN Leaky Integrate-and-Fire INT8)** con inhibición lateral Winner-Take-All (WTA) y telemetría real de actividad (`hasSNNActivity() -> lastActiveSpikes_ > 0`).<br/>• **Descomposición NMF en Tiempo Real** en 4 flujos ortogonales (*Vocal, Transiente, Armónico, Ambiente*) proyectados a **Ambisonics 3D de 4.º Orden (16 canales ACN/SN3D)** con rotación 6-DoF. |
| **Eje 4** | `PinnaManifoldInterpolator.hpp`<br/>*(Anti-Apple SIREN/INR)* | • **Manifold Implícito SIREN/INR de 6 Dimensiones** acoplado a la proyección antropométrica SOFA-SAF de 7 dimensiones.<br/>• **Síntesis FIR Binaural de Fase Mínima Estricta** (relación cepstral de Oppenheim-Schafer, 8 taps NEON) + síntesis de muesca espectral anatómica de concha/hélix ($6.5\text{–}10.5\text{ kHz}$) e ITD fraccional de Woodworth. |
| **Eje 5** | `ShmPipelineArbitrator.hpp`<br/>*(`SupremeMsoFarrowArbitrator`)* | • **Arbitraje Atómico Cross-Process** mediante `std::atomic_ref` CAS sobre `/ivanna_supreme_shm_v2` (`shm_open`/`mmap`).<br/>• **Interpolador Fraccional de Lagrange/Farrow de 5.º Orden** con resolución sub-nanosegundo y optimización Multi-Subgrave (MSO). |
| **Eje Supremo Coclear** | `CochlearActiveInverseModel.hpp`<br/>*(`CochlearActiveInverseEngine`)* | • **Inversión Biomecánica Activa de Células Ciliadas Externas (OHC / Prestina)** sobre **8 bandas críticas tonotópicas de Greenwood** ($\{120, 331, 710, 1390, 2613, 4807, 8736, 16000\}\text{ Hz}$).<br/>• Aplica la ley inversa libre de divisiones $H^{-1}(x) = x \cdot (1 - \alpha_b \cdot \text{env}_b^2)$ con integrador numérico **Heun RK2** (Runge-Kutta de 2.º orden), layout `alignas(64)` SoA y vectorización SIMD `float32x4_t`.<br/>• **Autoridad Única Coclear**: unificada en Ruta A y Ruta B para evitar cualquier doble procesamiento coclear, con default conservador (`OFF @ 0.35`) activable a voluntad desde la pestaña **NHO**. |

---

## ✦ 6. Hipermotor de Reconstrucción de Realidad Acústica (Fases 0–15) y los 7 Ejes Espaciales

### 6.1 Hipermotor de Reconstrucción de Realidad Acústica (`acoustic_reality_hyperengine.hpp` & `acoustic_cognitive_evolution_engine.hpp`)

Implementa una arquitectura de 6 capas físicas y cognitivas coordinadas por `RealityHyperEngine` y `AcousticRealityOrchestrator` (verificadas por `test_acoustic_reality_hyperengine`):

1. **Capa 1 · `TransducerPhysicsCompensator`**: Filtro FIR de fase mixta de 64 taps + compensación térmica de resistencia de bobina móvil ($\alpha_{\text{Cu}} = 0.00393\,\text{K}^{-1}$) y amortiguación de excursión no-lineal en graves.
2. **Capa 2 · `InverseRoomDereverberator`**: Predicción lineal ponderada multi-retardo (WPE) en sub-bandas para suprimir reflexiones tardías parásitas del recinto de grabación sin erosionar los ataques directos.
3. **Capa 3 · `Soundfield4DWfsSynthesizer`**: Síntesis de Campo de Ondas (WFS 2.5D/4D) con pre-filtro de fase estacionaria ($\sqrt{j k / 2\pi}$), compensación Doppler suavizada y renderizado binaural acoplado al seguimiento de cabeza.
4. **Capa 4 · `PerceptualLoudnessEqualizer`**: Contorno de igual sonoridad dinámica basado en **ISO 226:2023** con adaptación en tiempo real al volumen de escucha y compensación de pérdida de graves por falta de sellado de almohadilla.
5. **Capa 5 · `CognitiveIntentTracker`**: Inferencia bayesiana del estado de intención acústica (*Focus / Immersion / Relaxation / Analytical*) y estimación del índice de fatiga auditiva acumulada.
6. **Capa 6 · `SelfAuditingSafetyGuard`**: Auditoría continua en lazo cerrado del factor de cresta, margen de pico verdadero (True-Peak) y estabilidad numérica.

### 6.2 Arquitectura Acústica Espacial de 7 Ejes (`app/src/main/cpp/spatial/`)

| Eje | Archivo C++ | Implementación Real y Garantías de Tiempo Real |
|:---:|-------------|------------------------------------------------|
| **Eje 1** | `StereoObjectDecomposer.hpp` | Descomposición Mid/Side en tiempo real con filtros de 1 polo derivados de la tasa de muestreo real ($\sim 250\text{ Hz}$). Extrae 4 objetos continuos (`CENTER`, `LEFT`, `RIGHT`, `AMBIENT`) con coordenadas `ObjectPosition{x,y,z}` dinámicas y energía por objeto. Cero `malloc` (`objL_[4][4096]` pre-alocados). |
| **Eje 2** | `HrtfPersonalizer.hpp` | Modelo esférico de **Woodworth/Rayleigh** ($\tau(\theta) = \frac{a}{c}(\sin|\theta| + |\theta|)$) para el escalamiento anatómico de retardo interaural (`itdScale_`) a partir de la circunferencia craneal del usuario y cálculo de resonancia de muesca de pinna ($\lambda/4 = c / (4 d_{\text{pinna}})$). Conectado directamente a `ObjectSpatialRenderer`. |
| **Eje 3** | `RoomProjectionEngine.hpp` + `RirConvolver.cpp` | Convolución particionada no-uniforme overlap-save (**head de 512 muestras de respuesta directa inmediata** + cola de 16 384 muestras FDLP = **16 896 muestras totales**), matriz **True-Stereo de 4 caminos ($LL, LR, RL, RR$)**, `LateReverbSuppressor` + `PhysicalEarlyReflections` integrados con continuidad zero-pop ante el gobernador térmico, cancelación de diafonía transaural (**XTC** a $187.5\,\mu\text{s}$ / $2.2\text{ kHz}$) y crossfade de cambio de sala de $\sim 43\text{ ms}$. |
| **Eje 4** | `ObjectSpatialRenderer.hpp` | Ley física de distancia inversa ($1/d$), absorción atmosférica de altas frecuencias proporcional a la distancia, retardo interaural (ITD) esférico de Woodworth interpolado fraccionalmente y reflexiones tempranas físicas de 6 superficies (`PhysicalEarlyReflections`). |
| **Eje 5** | `PhysicalSceneRenderer.hpp` | Modelado físico de oclusión por obstáculos y absorción acústica de materiales mediante filtros Direct Form I libres de bifurcaciones en el bucle de muestras. |
| **Eje 6** | `HearingAdaptationEngine.hpp` | Compensación de fuga de sellado de almohadilla (`ear_tip_seal_factor`), corrección de presbiacusia (shelving de 2 bandas en $4\text{ kHz}$ y $8\text{ kHz}$) y atenuación progresiva contra fatiga auditiva (`setFatigueLevel`). Activo tanto en Ruta A como en Ruta B (`omega_effect.cpp`). |
| **Eje 7** | `IvannaAudioPipeline.hpp` + `PerfAuditor.hpp` | Pipeline espacial unificado (`alignas(16)`, punteros `__restrict`, cero `malloc` en `process()`) con medición empírica real de latencia algorítmica por detección de onset de impulso unitario (auditado por `MeasuredLatencyIsARealMeasurementNotAConstant`). |

### 6.3 Intelligent Upmixing (`IntelligentUpmixer` + `HoaBinauralDecoder`), `HybridRenderer` (128-tap) y `WfsRenderer`

- **Intelligent Upmixer (`spatial/IntelligentUpmixer.{hpp,cpp}`)**: Crossover complementario de 2.º orden (`bass + midHi == mid` exacto; graves mono-seguros sin cancelación de fase), codificación Ambisonics horizontal de orden 0–2 (9 canales ACN/SN3D) con bases de energía unitaria precalculadas, detector de transientes (`TransientDetector`: ataque $2\text{ ms}$ vs envolvente lenta $60\text{ ms}$) que ancla la percusión al centro, y decodificación binaural **`HoaBinauralDecoder` con ponderación $\max\text{-}\mathbf{r}_E$ de Daniel & Nicol** sobre el dataset HRTF medido.
- **Hybrid HRTF Renderer (`spatial/HybridRenderer.hpp`)**: Convolución FIR binaural de **128 taps activos (`kActiveTaps = 128`)** acelerada con intrínsecos ARM64 NEON (`vmlaq_f32`) e interpolación bilineal libre de clics entre direcciones esféricas.
- **Wave Field Synthesis (`spatial/WfsRenderer.{hpp,cpp}`)**: Array circular de 16 altavoces virtuales (escalable hasta 32) con función impulsora WFS 2.5D (atenuación cilíndrica $1/\sqrt{d} \times$ factor de ventana coseno), retardos fraccionales interpolados e integración con crossfade smoothstep $C^1$ ($3t^2 - 2t^3$) de $20\text{ ms}$ en `IvannaFusionEngine::process()`.

---

## ✦ 7. Entrenamiento Conjunto SOFA (255 AES69) + SAF (Manifold 7-D) + RIR (200 Salas) desde $t = 0\text{ ms}$

> **Generador reproducible:** `scripts/train_sofa_saf_rir_master.py` $\longrightarrow$ `app/src/main/cpp/spatial/SofaSafRirMasterKnowledge.hpp` + `pca_basis.bin` (`PCAV` v1, 7 180 bytes).

### 7.1 Inventario de Datasets Espaciales Medidos Incluidos en el Repositorio

| Familia | Activos Verificados en el Árbol | Formato y Validación en CI | Ubicación en Repo / Módulo |
|---------|---------------------------------|----------------------------|----------------------------|
| **HRTF (`.ihr1`)** | **12 datasets medidos** (MIT KEMAR normal/large pinna, TU-Berlin, CIPIC, SADIE II, FreeField…) | Binario `.ihr1` de 128 taps por oído con verificación de cabecera mágica y energía | `app/src/main/assets/ivanna_omega/hrtf/` + `magisk_module/system/etc/ivanna_omega/hrtf/` |
| **SOFA (AES69-2015)** | **255 archivos `.sofa` únicos** (671 instancias en árbol de assets + módulo: MIT KEMAR, CIPIC, GeneralTF, ARI HpIR) | HDF5 verificado byte a byte por firma `\x89HDF\r\n\x1a\n` en CI | `app/src/main/assets/` + `magisk_module/system/etc/ivanna_omega/sofa/` |
| **BRIR / RIR** | **200 salas reales medidas** (`rir_0000.wav` a `rir_0199.wav`) con tabla completa `kRirAcousticsTable[200]` | WAV PCM 16-bit estéreo + descriptores $\text{RT}_{60}, \text{DRR}, C_{80}, \text{IACC}_{\text{E/L}}$ | `app/src/main/assets/ivanna_omega/rir/` + `magisk_module/system/etc/ivanna_omega/rir/` |
| **SAF Manifold 7-D** | **214 sujetos antropométricos** proyectados sobre métrica de información de Fisher $\mathbf{G}_0$ | `SAF_model_total.json` + `pca_basis.bin` + extensión $C^1$ a 128 taps en `SofaSafRirMasterKnowledge.hpp` | `app/src/main/assets/ivanna_omega/` + `magisk_module/system/etc/ivanna_omega/` |

### 7.2 Los 5 Arquetipos Maestros de Sala BRIR y Auto-Calibración por Ruta ($t = 0\text{ ms}$)

Tanto en **Root (`omega_effect.cpp`)** como **Sin Root (`ivanna_omega_jni.cpp` + `PersistedStateRestorer.kt`)**, el motor nace pre-sembrado desde el milisegundo cero con el vector latente antropométrico maestro `kMasterSafGoldenQ` y el tensor de acoplamiento $\mathbf{W}_{\text{SOFA}\to\text{RIR}} \in \mathbb{R}^{4 \times 7}$, conmutando automáticamente el arquetipo de sala según el transductor físico activo:

| Sala Maestra | Archivo WAV | $\text{RT}_{60}$ | $\text{DRR}$ | $C_{80}$ | $\text{IACC}_{\text{Early}} / \text{IACC}_{\text{Late}}$ | Mezcla Wet | Ruta / Caso de Uso Automático |
|--------------|-------------|:----------------:|:------------:|:--------:|:--------------------------------------------------------:|:----------:|-------------------------------|
| **#51 · Golden Master Studio Control Room** | `rir_0051.wav` | **0.340 s** | **10.31 dB** | **16.66 dB** | **0.784 / 0.218** | **0.22** | **Arranque por defecto ($t=0\text{ ms}$) · AUX 3.5mm & DAC USB-C** |
| **#122 · Intimate Mastering Chamber** | `rir_0122.wav` | 0.451 s | 8.62 dB | 13.48 dB | 0.752 / 0.224 | 0.25 | Referencia Vocal / Acústica de Cámara |
| **#169 · Symphonic Concert Hall** | `rir_0169.wav` | 0.860 s | 5.84 dB | 8.91 dB | 0.718 / 0.218 | 0.30 | Orquestal / Cine 3D Gran Escala |
| **#81 · Open Speaker Projection Room** | `rir_0081.wav` | 0.613 s | 7.40 dB | 11.20 dB | 0.741 / 0.231 | 0.16 | Auto-Calibración para Altavoz Integrado (`SPEAKER`) |
| **#63 · Bluetooth Tight Anti-Codec Room** | `rir_0063.wav` | 0.293 s | 9.85 dB | 15.92 dB | 0.812 / 0.245 | 0.18 | Auto-Calibración para Bluetooth A2DP / LDAC (`BLUETOOTH`) |

---

## ✦ 8. Cadena DSP de 9 Etapas, Ruta Isócrona USB-C DAC y Bluetooth Master Path

### 8.1 Las 9 Etapas de la Cadena Base + Arbitraje Unificado

| # | Etapa | Archivo Principal | Procesamiento y Defensas de Producción Verificadas |
|:-:|-------|-------------------|----------------------------------------------------|
| **1** | **Pre-EQ Peak Guard** | `ivanna_omega_jni.cpp` | Headroom preventivo de $-1.0\text{ dBFS}$ con **ataque instantáneo y recuperación en rampa** (evita saturación interna en cascadas de filtros biquad cuando el usuario eleva múltiples bandas de EQ). |
| **2** | **ParametricEQ (10 Bandas)** | `dsp/ParametricEQ.cpp` | 10 filtros biquad RBJ en cascada con **crossfade anti-zipper de $15\text{ ms}$** al actualizar coeficientes y compensación automática de ganancia de headroom. |
| **3** | **Compressor** | `dsp/Compressor.cpp` | Detector RMS con filtro paso-alto en cadena lateral (sidechain HPF) para evitar bombeo (*pumping*) inducido por subgraves. |
| **4** | **HarmonicExciter (GoldenEar + Chebyshev)** | `dsp/HarmonicExciter.cpp` + `IvannaFusionCore.cpp` | Saturación racional de Padé ($\text{clamp }\pm 3$) con síntesis de 2.ª, 3.ª y 4.ª armónica Chebyshev ($T_2, T_3, T_4$), **oversampling $2\times$**, LPF de $14.5\text{ kHz}$, bloqueo DC de $5\text{ Hz}$, bypass bit-exacto a $\text{wet}=0$ y **slew-limiter por muestra** (`m_harmSmoothed_`, $\sim 167\text{ ms}$). |
| **5** | **StereoWidener & M/S Arbitrated** | `dsp/StereoWidener.cpp` + `IvannaFusionCore.cpp` | Imagen Mid/Side con clamp de correlación y **`setSpatialWidth` completamente cableado** en `IvannaFusionCore` con slew-limiter por muestra (`kWidthSlew = 1/4096`), normalización isométrica $1/\sqrt{0.5(1+w^2)}$ y **arbitraje automático**: se neutraliza ($w \to 1.0$) cuando HOA o WFS están activos para no deformar las claves binaurales ITD/ILD. |
| **6** | **PDEngine (NHO)** | `pd_engine.hpp` | Oscilador Armónico Neuromórfico (NHO) + `BiquadEnvelopeBank` + `CueBasedSpatial` con inhibición lateral entre bandas. |
| **7** | **GainStage** | `dsp/GainStage.cpp` | Trim maestro de salida suavizado muestra a muestra mediante filtro exponencial de un polo. |
| **8** | **SafetyLimiter + StabilityGuard** | `dsp/SafetyLimiter.cpp` + `SupremeAcousticStabilityGuard.hpp` | Limitador soft-knee adaptativo a la frecuencia de muestreo ($8\text{ kHz–}384\text{ kHz}$) respaldado por el gobernador isométrico de energía y el techo racional $C^2$ (`RationalC2SoftCeiling`). |
| **9** | **Red Final de Saneo NaN/Inf** | `ivanna_omega_jni.cpp` / `omega_effect.cpp` | Barrera incondicional antes del búfer del HAL/DAC: cualquier valor no finito se neutraliza a $0.0\text{f}$ e incrementa los contadores de telemetría auditables (`nanRecoveries` / `blkNanRecoveries`). |

### 8.2 Ruta Libre para DAC USB-C (`usb_audio_pro_manager.cpp`) y Bluetooth Master Path

- **Motor Isócrono USB-C Directo (`usbfs` UAC1/UAC2)**:
  - Al conectar un DAC USB-C, el receptor de manifiesto filtrado exclusivamente a dispositivos `USB_CLASS_AUDIO` solicita permiso UAC y abre el endpoint isócrono `OUT` mediante `USBDEVFS_SUBMITURB` / `USBDEVFS_REAPURBNDELAY` con **8 URBs en vuelo**, anillo SPSC lock-free y reloj asíncrono gobernado por el DAC.
  - **Negociación física real de ancho de banda**: calcula la tasa máxima admisible ($384\text{ kHz} \to 44.1\text{ kHz}$) a partir de `maxPacketSize` y `bInterval` (distinguiendo buses USB Full-Speed vs High-Speed). En desconexión en caliente (*hot-unplug*), ejecuta `DISCARDURB`, drena la sesión sin descriptores zombis y cancela la ventana de flush HRTF de $150\text{ ms}$ para evitar ruidos residuales.
- **Bluetooth Master Path**:
  - Guardia de cola de `AudioTrack` consciente de la ruta activa (A2DP / SCO / BLE Audio, cacheada cada $500\text{ ms}$) con umbral adaptativo de 6 bloques ($2\text{ ms}$) en Bluetooth frente a 3 bloques ($1\text{ ms}$) en rutas cableadas, eliminando los micro-cortes de resincronización con el reloj del códec Bluetooth, acompañado de la **Sala Maestra #63 (`rir_0063.wav`)** optimizada para preservar claridad ($C_{80} = 15.92\text{ dB}$) frente a códecs con pérdida.

---

## ✦ 9. Inteligencia Acústica, TinyML Neuromórfico y Asistente Cognitivo Gemini 2.5

### 9.1 Los Cerebros que Escuchan en Tiempo Real

| Subsistema | Archivo / Motor | Qué mide y qué decide en Producción |
|------------|-----------------|-------------------------------------|
| **IvannaMusicIntelligenceEngine** | `music_intelligence/IvannaMusicIntelligenceEngine.hpp` | Motor del **Atlas de Escena Musical (`v2.4.0`)**: extrae 12 características acústicas, clasifica y mezcla continuamente los 12 estilos maestros (`StyleBlender`) con pesos binarios `IVW1` (`8 774` parámetros) y publica `SceneTarget` vía `SceneTargetBus`. |
| **AdaptiveDecisionEngine** | `adaptive_decision_engine.hpp` | Hilo de control de $50\text{ ms}$ que evalúa factor de cresta, margen al limitador, EMA de sibilancia, $\text{RT}_{60}$ y `voice_score` para modular ganancia objetivo, compresión, reducción de excitador y apertura espacial sin escalones audibles. |
| **HeavyWorkerEngine** | `include/omega_wave_stages.hpp` | Ejecuta fuera del hilo RT la fusión neuromórfica completa: **`AntiDolbyAI` (`pi_lstm_milenio.hpp`)**, **`TinyMLAudioEngine`**, **`IvannaNeuromorphicTinyML` (`IvannaNeuromorphicTinyML.cpp`, 100% conectado en producción)**, **`LIFPool32` / `LIFPool128`**, **`AutonomousBrain`**, **`Synthesizer`**, **`SaFOptimizer`** y **`AcousticRealityOrchestrator`**. |
| **Kernel Evolutivo** | `evolutionary_kernel.cpp` | Población genética de **128 genomas $\times$ 256 genes** con elitismo, cruce y mutación que optimiza parámetros NHO y espaciales contra una función de aptitud (*fitness*) acoplada al audio real en curso. |
| **PsychoacousticAnalyzer** | `Psychoacoustics.cpp` | FFT real de 1024 puntos + **24 bandas críticas de Bark** + ponderación K IIR (**ITU-R BS.1770-4**) para cálculo de sonoridad LUFS verdadera y umbral de enmascaramiento simultáneo entre bandas. |
| **Clasificador CRNN & ConvNeXt** | `AntiDolbyCrnnClassifier` + `IvannaAudioClassifier` | Inferencia sobre espectrograma log-Mel $32 \times 40$ @ $16\text{ kHz}$ y bloques ConvNeXt 1D INT8/FP32 (`ivanna_weights.ivw1`) con suavizado EMA temporal para discriminar voz, música, graves, transientes y ambiente. |
| **QLearning & LearningBias** | `HybridDecisionEngine` | Bandido contextual $\varepsilon$-greedy sobre matriz *(emoción $\times$ fatiga)* + aprendizaje continuo del sesgo manual del usuario (`usuario − autónomo`) persistido en JSON-Lines auditable. |
| **CMA-ES ISO 226** | `Iso226CalibrationPanel` | Estrategia evolutiva de matriz de covarianza para calibrar el perfil psicoacústico individual del oyente. |

### 9.2 Asistente Cognitivo Híbrido con Gemini 2.5 Flash + Motor Agéntico Offline

- **Núcleo en la Nube con Gemini 2.5 Flash (`ai/gemini/IvannaGeminiAgent.kt` + `GeminiOrchestrator.kt`)**:
  - Integrado mediante Firebase AI Logic (`firebase-ai` sobre Firebase BoM `34.17.0`) y cliente REST directo con prompt de sistema especializado en ingeniería DSP de IVANNA.
  - **Cero llaves hardcodeadas**: el usuario introduce su propia API key en el panel del asistente (con botón **PROBAR CONEXIÓN** en vivo) y se almacena cifrada mediante `SecureConfigurationManager` (o se inyecta opcionalmente en compilación vía `BuildConfig.GEMINI_API_KEY`).
- **Motor Agéntico Offline 100% Funcional**:
  - Cuando no hay conexión a Internet o no se ha configurado una API key, el asistente conmuta de forma transparente al motor conversacional e intencional local (`IvannaMusicalIntentEngine`, `VoiceController`), ejecutando exactamente los mismos comandos `[CMD:...]` sobre el DSP nativo (`bass_boost`, `treble_reduce`, `auto_optimize`, `diagnose`, `clip_relief`, `music_mode`, `flat_mode`, cambios de sala BRIR y ajustes SAF).
- **Memoria Episódica y Semántica + Self-Healing**:
  - `IvannaContextMemory`, `IvannaSuperAgentMemory` y `MemoryRetrievalEngine` recuerdan las preferencias del usuario por género y contexto acústico; el motor de auto-reparación (`SelfHealingEngine`) audita telemetría real (`clipCountDelta`, `gainReductionDb`, `daemonConnected`) y ejecuta alivio automático de clipping (`relieveClipping()`).

---

## ✦ 10. Daemon Root, IPC Lock-Free SHM v5 y UI `Aurora Obsidiana`

### 10.1 Plano de Control Cross-Process (`ivanna_daemon` & `OmegaControlBus`)

| Componente | Implementación Técnica Verificada |
|------------|-----------------------------------|
| **`ivanna_daemon`** | Binario nativo ARM64 compilado con `PIE + Full RELRO + BIND_NOW + -static-libstdc++`. Se ejecuta como `root` desde Magisk/KernelSU con prioridad de tiempo real `SCHED_FIFO 98` y afinidad fijada al clúster eficiente en arquitecturas `big.LITTLE`. |
| **Doble Canal de Socket** | Socket Unix abstracto primario `@omega_daemon_socket` + canal dedicado `@omega_command_socket` **con fallback automático a TCP loopback `127.0.0.1:12121`** si la ROM o políticas OEM restringen el espacio de nombres abstracto. |
| **Memoria Compartida Seqlock v5** | `OmegaControlBus` (`/data/adb/ivanna_omega/omega_control_snapshot`, 512 bytes alineados a línea de caché) protegido por `MAGIC + VERSION (v5) + CRC32` y protocolo Seqlock wait-free para el lector de audio. |
| **Hiperplano SHM v2** | `/ivanna_supreme_shm_v2` (`shm_hyperplane.cpp` + `ShmPipelineArbitrator.hpp`) con operaciones atómicas `std::atomic_ref` CAS entre el proceso de la aplicación y `audioserver`. |
| **Arbitraje de Ruta (`RouteArbiter`)** | Estado explícito `OFF / IN_PROCESS / SYSTEM_WIDE` sincronizado con telemetría inversa (`raw_rms`, `raw_peak`, `effect_frames` escritos por `audioserver`) para confirmar que `libomega_effect.so` está procesando frames reales. |
| **Gobernador Térmico (`ThermalGovernor`)** | 5 escalones de degradación elegante que ajustan el orden Ambisonics y la longitud de cola FIR ante estrangulamiento térmico del SoC, acoplados a `SupremeTransitionEnvelope` para mantener continuidad libre de pops. |
| **Política SELinux Persistente** | 153 reglas `allow` en `sepolicy.rule` aplicadas durante la instalación del módulo **y reaplicadas en cada arranque** desde `magisk_module/service.sh`. |

### 10.2 Instrumentación Visual en Jetpack Compose (`221` archivos Kotlin, `52.2K LOC`) y Consola Web

- **42+ pantallas Jetpack Compose** bajo el sistema de diseño **Aurora Obsidiana**, estructuradas en las vistas `CONTROL`, `BRAIN`, `ATLAS DE ESCENA (MusicIntelligencePanel)`, `ADAPTIVE`, `SPATIAL`, `SYSTEM`, `NHO / 5 EJES SUPREMOS` y la suite de diagnóstico `OEM` (`OemAcousticScreen`, `OemAiScreen`, `OemSpatialScreen`, `OemTelemetryScreen`, `OemThermalScreen`).
- **Visualizador OpenGL / Gammatone de 64 Bandas Bark Reales** (`visualizer/gammatone_lattice.hpp`, `gl_uniform_bridge_bark64.hpp`, `Bark64VisualizerPanel.kt`).
- **Ivanna LAB (`IvannaLab`)**: banco de pruebas integrado en el dispositivo para medir THD+N, IMD, sonoridad LUFS BS.1770-4, SNR, True-Peak y pruebas ciegas **ABX**.
- **Consola Web Interactiva (`src/`, React 19 + TypeScript + Vite + Tailwind CSS 4)**: simulador y panel de inspección arquitectónica ejecutable en navegador para visualización de telemetría, diseño de curvas y exportación de bloques C++20 para Termux.

---

## ✦ 11. Honestidad Radical de Ingeniería: Lo que IVANNA Hace y lo que NO Hace

Este proyecto mantiene una política estricta de **cero inflación de marketing** validada automáticamente por `scripts/check_docs_claims.py`: cada afirmación de este documento corresponde a código fuente real compilado y auditado en CI, y las limitaciones físicas o de sistema operativo se declaran explícitamente:

1. **En dispositivos sin Root (Ruta A), las políticas DRM de Android se respetan**:
   - Sin Root no existe inyección dentro del proceso del sistema `audioserver` (Ruta B). En modo sin Root, el reproductor interno de IVANNA, la salida USB-C DAC directa y todas las aplicaciones que permiten `MediaProjection` ejecutan el **100% del motor C++20 (`libivanna_omega.so`), el `OmniHolographicSingularityEngine`, el `SupremeAcousticStabilityGuard`, el Atlas de Escena Musical, los 5 Ejes Supremos y las 200 salas BRIR**, mientras que las apps que bloquean explícitamente la captura por DRM solo reciben los efectos estándar de sesión de Android.
2. **Offloading Hexagon cDSP (`app/src/main/cpp/hexagon/ivanna_dsp.cpp`)**:
   - El cargador dinámico `dlopen` (`libcdsprpc.so` / `libadsprpc.so`), el contrato IDL unificado (`ivanna_dsp.idl`) y el puente JNI (`nativeDsp*`) están implementados, saneados contra corrupción de heap y enlazados con `-z defs`. Sin embargo, dado que el binario `skel` firmado por QAIC del SDK propietario de Qualcomm depende de la firma OEM de cada fabricante, **el procesamiento de audio en producción se ejecuta al 100% sobre la CPU ARM64 + SIMD NEON**, y la telemetría reporta honestamente cuando el cDSP no está activo en lugar de simularlo.
3. **Contadores Hardware PMU (`PMCCNTR_EL0`)**:
   - En la mayoría de kernels Android de consumo, el registro de ciclos de hardware `PMCCNTR_EL0` está restringido a `EL1` (kernel). Cuando el kernel bloquea su lectura en espacio de usuario, el perfilador reporta honestamente `N/M` en lugar de inventar cifras de GFLOPS.
4. **Métricas Perceptuales de Referencia Externa (PEAQ / ViSQOL) e Inversión de Sala**:
   - En `PerfAuditor.hpp`, los campos `peaq_score` y `visqol_score` están marcados explícitamente como constantes de referencia no medidas en tiempo real porque los estándares ITU-R BS.1387 (PEAQ) y ViSQOL requieren comparar contra una señal maestra externa de laboratorio. Por el contrario, **la latencia algorítmica, el THD, el factor de cresta, la conservación de energía y la ausencia de NaN/clipping sí se miden empíricamente muestra a muestra**.
   - Asimismo, en `RoomProjectionEngine.hpp`, la etapa de de-reverberación estadística (`LateReverbSuppressor`) opera con un crossover de 3 polos a $185\text{ Hz}$ y estimadores duales de envolvente, mientras que la predicción lineal WPE multibanda vive en `InverseRoomDereverberator` dentro de `acoustic_reality_hyperengine.hpp`.
5. **Defaults Conservadores libres de Artefactos**:
   - Para garantizar transparencia absoluta desde el primer arranque, la inversión coclear (`Cochlear-PINN`) inicia en bypass suave (`OFF @ 0.35`) hasta que el usuario la activa en el panel NHO, mientras que la calibración espacial arranca en la Sala Maestra `#51` con mezcla equilibrada (`wet = 0.22`).

---

## ✦ 12. IVANNA OMEGA SUPREME frente a los Estándares Comerciales de la Industria

| Dimensión Técnica | Dolby Atmos Mobile | Apple Spatial Audio | Sony 360RA / DSEE Ultimate | Dirac Live / Virtuo | **IVANNA OMEGA SUPREME (`v2.4.0`)** |
|-------------------|--------------------|---------------------|----------------------------|---------------------|--------------------------------------|
| **Arquitectura de Espacialización** | Upmixer paramétrico + reverberador sintético fijo | HRTF genérica + escaneo TrueDepth (cerrado a hardware Apple) | Selección discreta de perfil por foto en la nube | Corrección FIR/IIR de fase mixta con latencia de bloque | **Ambisonics 4.º Orden (16 ch) + WFS 2.5D/4D + 255 SOFA + Manifold 7-D SAF + 200 BRIR reales True-Stereo 4x + ER Físicas de 6 Paredes** |
| **Fusión IA $\leftrightarrow$ DSP Espacial** | Reglas estáticas por perfil (*Movie / Music / Game*) | Sin lazo cerrado entre clasificador semántico y campo de onda | CNN aislada en magnitud sin control del campo espacial | Sin inferencia neuromórfica en tiempo real | **Lazo cerrado `HeavyWorkerEngine` + `Atlas de Escena (12 estilos IVW1)` $\leftrightarrow$ `SceneTargetBus` $\leftrightarrow$ `OmniHolographicSingularityEngine`** |
| **Protección de Techo y Transiciones** | Limitador dinámico que aplasta el factor de cresta | Limitador de salida propietario sin control de usuario | Recorte suave convencional | Limitador de pico estándar | **`RationalC2SoftCeiling` (Identidad 1:1 hasta $\pm 0.88\text{ FS}$, $\text{THD}=0.00000\%$) + `HermiteC1BoundaryStitcher` + `IsometricEnergyGovernor`** |
| **Restauración Armónica y Transientes** | Compresión multibanda agresiva | EQ adaptativo sin síntesis trans-armónica | CNN en magnitud (sin coherencia de derivada de fase) | Sin síntesis trans-armónica ni modelo coclear | **CVNN `modReLU` + Polinomios Chebyshev $T_2/T_3/T_4$ + 8 osciladores DDSP NEON + Cinta 2" Jiles-Atherton + De-Clipper Hermite + Inversión Coclear PINN** |
| **Corrección Física de Transductor** | Curva EQ fija por perfil XML del fabricante | EQ adaptativo cerrado a AirPods | Perfiles EQ pregrabados para audífonos Sony | Filtro FIR de fase mixta (requiere medición externa) | **Celosía deformada Bark ($\lambda=0.72$) + linealización Lorentz $Bl(x)$ + adaptación NLMS en hipercubo de Schur** |
| **Latencia Algorítmica del Núcleo** | $15\text{–}40\text{ ms}$ (ventanas STFT) | $12\text{–}30\text{ ms}$ | $20\text{–}45\text{ ms}$ (inferencia CNN por bloque) | $5\text{–}25\text{ ms}$ (convolución FIR lineal) | **Procesamiento directo muestra a muestra (0 muestras de lookahead) en los 5 Ejes Supremos, Cochlear-PINN y partición 0 (head de 512 muestras) del convolver BRIR** |
| **Ejecución y Apertura en Android** | Binario propietario cerrado a ROMs con licencia | Inexistente en Android | Limitado a apps/dispositivos certificados | Cerrado a acuerdos OEM | **100% Nativo Android ARM64 (Ruta A Sin Root + USB DAC Isócrono + Ruta B Global Magisk/KernelSU)** |

---

## ✦ 13. Matriz de Verificación Determinista (`6 Puertas + 174/174 CTest`) e Instalación

<div align="center">
<img src="docs/release_media/ivanna_verification_matrix.svg" alt="Matriz de Verificación Determinista de 6 Puertas Estáticas, 174/174 CTest y Release v2.4.0" width="100%" />
</div>

### 13.1 Las 6 Puertas Estáticas de Ingeniería (`scripts/check_*.py`)

Cada commit es auditado por 6 analizadores estáticos deterministas que bloquean cualquier regresión arquitectónica:

| Puerta | Script Auditor | Métrica Verificada en `v2.4.0` | Estado |
|:------:|----------------|:------------------------------:|:------:|
| **`[A3]`** | `scripts/check_rt_safety.py` | **34/34** puntos de entrada RT y sus callees de 1.er y 2.º nivel libres de `malloc`, `free`, `new`, `delete`, `mutex`, `syslog` o `fopen` | ✅ `PASS` |
| **`[0.4]`** | `scripts/check_build_flags.py` | **6/6** archivos CMake/Gradle auditados con **0** flags IEEE-754 destructivos (`-ffast-math`, `-Ofast`, `-ffinite-math-only`) | ✅ `PASS` |
| **`[0.2]`** | `scripts/check_header_wiring.py` | **147 de 147** headers de producción (`.h`/`.hpp`) alcanzables desde las unidades de traducción compiladas (**0** huérfanos) | ✅ `PASS` |
| **`[0.1]`** | `scripts/check_jni_wiring.py` | **321 de 321** declaraciones `external fun` en Kotlin emparejadas biyectivamente 1:1 con **321** símbolos `Java_*` en **96** fuentes C/C++ | ✅ `PASS` |
| **`[A2]`** | `scripts/check_kotlin_wrapper_wiring.py` | **232 de 232** wrappers públicos de `OmegaEngineBridge` cuentan con al menos un llamador real en la app/servicios (**0** código muerto) | ✅ `PASS` |
| **`[A5]`** | `scripts/check_docs_claims.py` | Sincronía documental estricta contra `CMAKE_CXX_STANDARD=20`, `HybridRenderer::kActiveTaps=128`, puertas A3/A4 y `telemetry/ctest_summary.json` (`174/174`) | ✅ `PASS` |

### 13.2 Estado Verificado de Integración Continua en GitHub Actions (`v2.4.0`)

| Flujo / Suite de Verificación | Estado en GitHub Actions | Cobertura Técnica Auditada |
|-------------------------------|:------------------------:|----------------------------|
| **Datasets HRTF, SOFA & IVW1 (`tests-host.yml`)** | ✅ `SUCCESS` | Validación binaria de los 12 archivos `.ihr1`, firma HDF5 (`894844460d0a1a0a`) de los 255 `.sofa`, `pca_basis.bin`, las 200 salas WAV BRIR y `ivanna_weights.ivw1` (`8 774` parámetros). |
| **Host CTest Suite (`174/174` Tests)** | ✅ `SUCCESS` (`16.48 s`) | **174/174 tests en verde**, incluyendo `UnifiedMasterV3SafetyBench` (Fases 1–8), `test_supreme_five_axes`, `test_acoustic_reality_hyperengine`, `test_cochlear_inverse_model`, `test_supreme_zero_pop_transition`, `test_supreme_acoustic_continuity`, `test_ime_style_blender`, `test_scene_bus`, `test_late_reverb_suppressor`, `test_woodworth_itd`, `test_er_tap_time`, `test_cheb_shaper`, `test_cold_start_sim` y `test_rt_no_alloc`. |
| **Host CTest (`ASan + UBSan`)** | ✅ `SUCCESS` | Ejecución completa de las **174/174** pruebas bajo **AddressSanitizer + UndefinedBehaviorSanitizer** con 0 fugas de memoria, 0 accesos fuera de límites y 0 comportamientos indefinidos. |
| **Build APK & Native Binaries (`build.yml`)** | ✅ `SUCCESS` | Compilación con Android NDK ARM64 (`arm64-v8a`), verificación de flags de seguridad ELF (`PIE`, `Full RELRO`, `BIND_NOW`), sincronización estricta de `version.properties` (`v2.4.0 / 240`) y empaquetado de artefactos. |
| **Verify & Publish GitHub Release (`v2.4.0`)** | ✅ `SUCCESS` | Extracción, validación de integridad de artefactos y publicación automática de `ivanna_omega_supreme_v2.4.0.zip` (Módulo Magisk/KernelSU) y el APK firmado para `magisk_module/update.json`. |

### 13.3 Guía de Instalación Rápida (Con Root y Sin Root)

1. **Descarga Oficial (`Release v2.4.0`)**:
   - Ve a la sección [**Releases**](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/releases/tag/v2.4.0) y descarga el APK y (si tienes Root) `ivanna_omega_supreme_v2.4.0.zip`.
2. **En Dispositivos CON Root (Magisk / KernelSU — Ruta B Global + Ruta A)**:
   - Flashea `ivanna_omega_supreme_v2.4.0.zip` desde la app de Magisk o KernelSU y **reinicia el dispositivo**.
   - Instala el APK de **IVANNA OMEGA SUPREME**. Al abrirla, el daemon `ivanna_daemon` (`SCHED_FIFO 98`) y `libomega_effect.so` ya estarán activos dentro de `audioserver` con la calibración maestra `#51` y el Atlas de Escena desde $t = 0\text{ ms}$.
3. **En Dispositivos SIN Root (Ruta A En Proceso + Captura + USB-C DAC Directo)**:
   - Instala únicamente el APK de **IVANNA OMEGA SUPREME**.
   - Abre la app y concede el permiso de captura si deseas procesar el audio de otras aplicaciones vía `MediaProjection`, o conecta tu **DAC USB-C** para activar la ruta isócrona directa (`usbfs`).
4. **Activación Opcional de Gemini 2.5 Flash**:
   - En la pestaña del Asistente, introduce tu propia API key de Google Gemini y pulsa **PROBAR CONEXIÓN**. Si prefieres operar 100% sin red o sin llave, el motor cognitivo offline responde y ejecuta todos los comandos DSP localmente.

### 13.4 Comandos de Verificación y Compilación para Desarrolladores

```bash
# 1. Ejecutar las 6 puertas estáticas de calidad (RT-Safety, Flags, Headers, JNI, Kotlin, Docs)
python3 scripts/check_rt_safety.py && \
python3 scripts/check_build_flags.py && \
python3 scripts/check_header_wiring.py && \
python3 scripts/check_jni_wiring.py && \
python3 scripts/check_kotlin_wrapper_wiring.py && \
python3 scripts/check_docs_claims.py

# 2. Ejecutar la suite completa de 174/174 tests nativos C++20 en host (normal)
bash scripts/run_ctest.sh

# 3. Ejecutar la suite completa bajo AddressSanitizer + UndefinedBehaviorSanitizer
IVANNA_SAN=asan bash scripts/run_ctest.sh

# 4. Regenerar pesos binarios IVW1 del Atlas de Escena y el entrenamiento 255-SOFA + 7D-SAF + 200-RIR
python3 tools/train_atlas_em.py --synthetic --out app/src/main/assets/models/ivanna_weights.ivw1
python3 scripts/train_sofa_saf_rir_master.py

# 5. Compilar el APK Android (arm64-v8a, C++20 + SIMD ARM NEON)
./gradlew assembleDebug
```

---

<div align="center">

### ⬡ Autoría y Propiedad Intelectual ⬡

**Creado, Arquitecturado y Desarrollado por**
## **Luis Uriel Pimentel Pérez — GORE TNS**

*Ingeniería neuroacústica de precisión, física de transductores, inteligencia neuromórfica y programación de sistemas C++20 / ARM64 NEON.*
*Construido muestra a muestra. Auditado commit a commit. Verificado línea por línea.*

**© 2026 Luis Uriel Pimentel Pérez — GORE TNS. Todos los derechos reservados.**

</div>
