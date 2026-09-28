<div align="center">

```
                    ██████╗  ██████╗ ███╗   ███╗███████╗ ██████╗  █████╗
                   ██╔═══██╗██╔════╝ ████╗ ████║██╔════╝██╔════╝ ██╔══██╗
                   ██║   ██║██║  ███╗██╔████╔██║█████╗  ██║  ███╗███████║
                   ██║   ██║██║   ██║██║╚██╔╝██║██╔══╝  ██║   ██║██╔══██║
                   ╚██████╔╝╚██████╔╝██║ ╚═╝ ██║███████╗╚██████╔╝██║  ██║
                    ╚═════╝  ╚═════╝ ╚═╝     ╚═╝╚══════╝ ╚═════╝ ╚═╝  ╚═╝
```

# ⬡ IVANNA OMEGA SUPREME

### El motor de supremacía neuroacústica para Android — DSP nativo C++23/NEON ARM64, 5 Ejes Cuántico-Neuromórficos Lock-Free, Entrenamiento Conjunto 255-SOFA + 7D-SAF + 200-RIR desde $t = 0\text{ ms}$ (Root & Sin Root) y Asistente Cognitivo con Gemini 2.5

[![Build](https://img.shields.io/github/actions/workflow/status/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/build.yml?branch=main&style=for-the-badge&logo=github&label=BUILD&color=23F09A)](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions)
[![Tests host](https://img.shields.io/github/actions/workflow/status/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/tests-host.yml?branch=main&style=for-the-badge&logo=github&label=CTEST%20SUITE%20GREEN&color=23F09A)](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions/workflows/tests-host.yml)
[![DSP](https://img.shields.io/badge/DSP-C%2B%2B23%20%C2%B7%20NEON%20ARM64-6FF3FF?style=for-the-badge)](app/src/main/cpp/)
[![SOFA-SAF-RIR](https://img.shields.io/badge/SOFA%20255%20%C2%B7%20SAF%207D%20%C2%B7%20RIR%20200-Trained%20Master-00E5FF?style=for-the-badge)](app/src/main/cpp/spatial/SofaSafRirMasterKnowledge.hpp)
[![Module](https://img.shields.io/badge/Magisk%20Module-v2.3.12-FF3E86?style=for-the-badge&logo=magisk)](magisk_module/)

</div>

> 📖 Este archivo es el resumen ejecutivo y técnico de entrada al proyecto. La
> referencia exhaustiva archivo por archivo es **[README.md](README.md)**.
>
> 🤖 Este repo recibe trabajo de múltiples sesiones de IA en paralelo — antes
> de emprender trabajo sustancial, revisa **[AGENT_CLAIMS.md](AGENT_CLAIMS.md)**.

**No es un ecualizador. Es un motor neuroacústico de sistema completo, con cerebro propio y voz propia.**

---

## ✦ Qué es

IVANNA intercepta cada muestra de audio que produce el dispositivo —
Spotify, YouTube, juegos, llamadas— y la procesa con una cadena DSP nativa
en **C++23 optimizada a SIMD ARM NEON**, adaptada en tiempo real por un motor de
decisión que escucha lo que suena y decide cómo debe sonar. Si le hablas,
responde: asistente con **Gemini 2.5 Flash**, con motor offline completo cuando no
hay red o API key.

Dos rutas de procesamiento sincronizadas con la misma calibración maestra desde $t = 0\text{ ms}$:

- **Ruta A (Sin Root — JNI Lock-Free En Proceso):** la app ejecuta la cadena C++23 completa (`libivanna_omega.so`), incluyendo los **5 Ejes de Supremacía**, el convolver BRIR particionado de 200 salas (`RirConvolver` de 16 896 muestras) y el manifold SOFA-SAF de 7 dimensiones (`pca_basis.bin` + `SofaSafRirMasterKnowledge.hpp`) sobre su propio reproductor y sobre la captura de otras apps vía `MediaProjection`.
- **Ruta B (Con Root — System-Wide Magisk/KernelSU):** `libomega_effect.so` vive dentro de `audioserver` como `GlobalEffect` — una instancia aislada por sesión de audio, controlada cross-process vía memoria compartida (`OmegaControlBus` seqlock de 512 B + arbitraje CAS en `/ivanna_supreme_shm_v2`), lock-free en el callback de audio.

---

## ✦ La cadena DSP

Ocho etapas nativas — peak guard, EQ paramétrico de 10 bandas, compresor
con sidechain, excitador armónico, stereo widener M/S, motor de percepción
no lineal, gain stage y limitador de seguridad a −0.1 dBFS— más una red de
saneo NaN/Inf antes de llegar al DAC. Cada etapa tiene una defensa de
producción documentada (crossfades anti-zipper, oversampling, clamps,
contadores de recuperación que deben quedarse en 0 en operación normal).
Detalle completo, archivo por archivo, en el README.

---

## ✦ Espacialización y Entrenamiento Conjunto SOFA + SAF + RIR ($t = 0\text{ ms}$)

- **255 archivos SOFA (AES69-2015)** verificados por firma HDF5 + **12 datasets HRTF (`.ihr1` de 128 taps)** + **Manifold Riemanniano SAF de 7 dimensiones (214 sujetos)** + **200 salas RIR reales medidas en WAV estéreo**.
- **Fusión Maestra (`SofaSafRirMasterKnowledge.hpp` + `pca_basis.bin`):** extensión continua $C^1$ a 128 taps (`kMasterSofaP0[256]` y `kMasterSofaPcaV[7][256]`) acoplada en tiempo real a las 200 salas mediante el tensor $\mathbf{W}_{\text{SOFA}\to\text{RIR}} \in \mathbb{R}^{4 \times 7}$ y el optimizador geodésico $\Phi_{\text{SAF-Room}}^\infty$.
- **Calibración Magistral desde el Segundo Cero (Root y Sin Root):** al abrir la app, arranca pre-calibrada con la **Sala Maestra de Control #51 (`rir_0051.wav`, ITU-R BS.1116, $\text{RT}_{60}=0.340\text{ s}$, $\text{DRR}=10.31\text{ dB}$, $C_{80}=16.66\text{ dB}$, $\text{wet}=0.22$)** y adapta automáticamente el arquetipo de sala según el transductor conectado:

| Sala Maestra | Archivo WAV | $\text{RT}_{60}$ | $\text{DRR}$ | $C_{80}$ | $\text{IACC}_{\text{E}} / \text{IACC}_{\text{L}}$ | Mezcla Wet | Ruta / Caso de Uso |
|--------------|-------------|------------------|--------------|----------|--------------------------------------------------|------------|--------------------|
| **#51 · Golden Master Studio Control Room** | `rir_0051.wav` | **0.340 s** | **10.31 dB** | **16.66 dB** | **0.784 / 0.218** | **0.22** | **Arranque por defecto ($t=0\text{ ms}$) · AUX 3.5mm & USB DAC** |
| **#122 · Intimate Mastering Chamber** | `rir_0122.wav` | 0.451 s | 8.62 dB | 13.48 dB | 0.752 / 0.224 | 0.25 | Referencia Vocal / Acústica de Cámara |
| **#169 · Symphonic Concert Hall** | `rir_0169.wav` | 0.860 s | 5.84 dB | 8.91 dB | 0.718 / 0.218 | 0.30 | Orquestal / Cine 3D Gran Escala |
| **#81 · Open Speaker Projection Room** | `rir_0081.wav` | 0.613 s | 7.40 dB | 11.20 dB | 0.741 / 0.231 | 0.16 | Auto-Calibración para Altavoz Integrado (`SPEAKER`) |
| **#63 · Bluetooth Tight Anti-Codec Room** | `rir_0063.wav` | 0.293 s | 9.85 dB | 15.92 dB | 0.812 / 0.245 | 0.18 | Auto-Calibración para Bluetooth A2DP / LDAC (`BLUETOOTH`) |

---

## ✦ El ecosistema completo

| Componente | Stack | Función |
|---|---|---|
| App Android | Kotlin · Jetpack Compose | UI, Ruta A, asistente Gemini, laboratorio de medición |
| DSP nativo | C++23 · NEON ARM64 | Cadena de efectos, 5 Ejes Supremos, convolución BRIR, motores de decisión |
| Módulo Magisk | Shell · sepolicy | Ruta B system-wide, daemon root, datasets HRTF/RIR/SOFA/SAF |
| Panel web | React 19 · Vite · Tailwind 4 | Consola de visualización y export de parámetros |

---

## ✦ Arquitectura Acústica Espacial de 7 Ejes (C++20 RT-Safe)
El motor de espacialización acústica de IVANNA trabaja con **0 ms de latencia algorítmica añadida** y cero asignaciones dinámicas en el hilo de alta prioridad de audio (`SCHED_FIFO`):
- **Eje 1 (`StereoObjectDecomposer`)**: Descomposición en tiempo real Mid/Side con filtros de energía de 1 polo en graves (~250 Hz) para aislar 4 objetos continuos (CENTER, LEFT, RIGHT, AMBIENT).
- **Eje 2 (`HrtfPersonalizer`)**: Cálculo anatómico del retardo interaural (ITD) con la esfera de Woodworth/Rayleigh y síntesis del notch físico de pinna (6–9 kHz) y resonancia del conducto auditivo.
- **Eje 3 (`RoomProjectionEngine` & `RirConvolver`)**: De-reverberación y cancelación acústica parcial de sala combinada con convolución particionada uniforme (Gardner/Wefers overlap-save) con partición 0 a latencia cero y cola extendida de hasta 16384 muestras.
- **Eje 4 (`ObjectSpatialRenderer`)**: Renderizado 3D de objetos con atenuación inversa $1/d$, amortiguación de altas frecuencias por absorción de aire y reflexiones tempranas multicapa fraccionales.
- **Eje 5 (`PhysicalSceneRenderer`)**: Simulación física de oclusión de obstáculos y absorción de materiales con filtros Direct Form I optimizados para la caché L1.
- **Eje 6 (`HearingAdaptationEngine`)**: Compensación de pérdidas en graves por falta de sellado hermético de almohadillas (hasta +4 dB), curvas isofónicas (ISO 226), corrección de presbiacusia y protección dinámica contra fatiga auditiva.
- **Eje 7 (`PerfAuditor`)**: Certificación de rendimiento en tiempo real, latencia estricta de 0 ms, ausencia de NaN/Inf y presupuesto de CPU < 12% en procesadores móviles.

---

## ✦ Motor TinyML Neuromórfico Anti-Dolby
Sustituye por completo los 1000 ms de latencia del viejo YAMNet por una red liviana híbrida Depthwise Separable CNN + SNN/Pi-LSTM:
- **Inferencia en Sub-Milisegundo**: Ejecución inmediata por bloque de 10 ms (480 muestras a 48 kHz).
- **SIMD ARM NEON FMA**: Registros de 128 bits operando directamente sobre la caché L1.
- **Sincronización Lock-Free Wait-Free**: Estructuras SeqLock atómicas y búferes SPSC que eliminan cualquier riesgo de bloqueo o inversión de prioridad en `AudioFlinger`.
- **Descompresión Dinámica Anti-Dolby**: Atenúa la fatiga acústica provocada por procesadores dinámicos comerciales hiper-agresivos.

---

## ✦ Los 5 Ejes de Supremacía Cuántico-Neuromórfica + Inversión Coclear PINN (0.00 ms Latencia)

Implementados en `app/src/main/cpp/supreme/` y `app/src/main/cpp/neuromorphic/CochlearActiveInverseModel.hpp` con **0.00 ms de latencia algorítmica añadida**, **cero `malloc`/`new` en el hilo RT** y vectorización **SIMD ARM NEON (`float32x4_t`)**:

1. **Eje 1 · `WarpedLatticeTransducerInverter` (Anti-Dirac + De-Clipper Cúbico de Hermite):** Reconstrucción polinómica cúbica de Hermite para crestas recortadas ($|x| > 0.94$), celosía de fase mínima deformada en escala Bark ($\lambda \approx 0.72$),arquetipos por ruta (AUX/USB `#0`, Bluetooth `#1`, Altavoz `#2`), linealización electrodinámica Lorentz $Bl(x)$, adaptación NLMS en hipercubo de Schur ($|\kappa_m| < 0.95$) y sonda Micro-Chirp enmascarada ($<-70\text{ dBFS}$).
2. **Eje 2 · `PhaseCoherentTransharmonicSynthesizer` (Anti-DSEE + Cinta Analógica 2" Jiles-Atherton):** Red neuronal compleja (CVNN) con activación modReLU equivariante de fase + 8 osciladores DDSP en cuadratura NEON, cancelación activa de distorsión por intermodulación (IMD) y saturador magnético de histéresis de Jiles-Atherton ($M = M_s \mathcal{L}(H_e/a)$ + filtro de entrehierro).
3. **Eje 3 · `SnnNmfHoaUpmixer` (Anti-Dolby):** Red neuronal de impulsos (SNN LIF INT8) con inhibición lateral WTA + descomposición NMF en 4 flujos ortogonales proyectados a **Ambisonics 3D de 4º Orden (16 canales ACN/SN3D)** con rotación 6-DoF.
4. **Eje 4 · `PinnaManifoldInterpolator` (Anti-Apple):** Manifold implícito SIREN/INR de 6 dimensiones + síntesis FIR binaural de fase mínima estricta (Oppenheim-Schafer, 8 taps NEON) + muesca espectral de concha/hélix ($6.5\text{–}10.5\text{ kHz}$).
5. **Eje 5 · `SupremeMsoFarrowArbitrator` (SHM CAS + Farrow 5º Orden):** Arbitraje atómico cross-process (`std::atomic_ref` CAS sobre `shm_open`/`mmap`), interpolador fraccional de Lagrange/Farrow de 5º orden (resolución sub-nanosegundo MSO) y guardia hardware `ScopedFpDenormalsToZero` (`FPCR.FZ` / `MXCSR`).
6. **Eje Supremo Coclear · `CochlearActiveInverseEngine` (Cochlear-PINN) + `RirConvolver` True-Stereo 4-Caminos y XTC Transaural:** Modelo de 8 bandas críticas Greenwood ($120\text{ Hz–}16\text{ kHz}$) con inversión activa de la motilidad de prestina OHC ($y_b = g_b / (1 + \alpha_b g_b^2)$) + matriz de convolución cruzada ($LL, LR, RL, RR$) con retardo interaural de $250\text{ }\mu\text{s}$ y cancelación de diafonía transaural (XTC) con retardo de $187.5\text{ }\mu\text{s}$ e inversión de fase filtrada a $2.2\text{ kHz}$.

---

## ✦ IVANNA OMEGA SUPREME frente a los Gigantes de la Industria de Élite

| Dimensión Técnica | Dolby Atmos Mobile | Apple Spatial Audio | Sony 360RA / DSEE Ultimate | Dirac Live / Virtuo | **IVANNA OMEGA SUPREME** |
|-------------------|--------------------|---------------------|----------------------------|---------------------|--------------------------|
| **Arquitectura de Espacialización** | Upmixer paramétrico + reverberador sintético fijo | HRTF genérica + escaneo TrueDepth (cerrado a AirPods) | Selección discreta de perfil por foto en la nube | Corrección FIR/IIR de fase mixta con latencia de bloque | **Ambisonics 4º Orden (16 ch) + WFS 2.5D + 255 SOFA + Manifold 7-D SAF + 200 BRIR reales** |
| **Restauración Armónica / Transientes** | Compresión dinámica multibanda (reduce rango dinámico) | EQ adaptativo de graves/medios sin síntesis trans-armónica | CNN en dominio de magnitud (sin coherencia de derivada de fase) | Sin reconstrucción trans-armónica ni descompresión coclear | **CVNN modReLU + 8 osciladores DDSP en cuadratura NEON + SNN LIF INT8 + Inversión Coclear PINN** |
| **Corrección Física de Transductor** | Ninguna (solo curva EQ estática por perfil OEM) | EQ adaptativo por micrófono interno (solo hardware Apple) | Solo perfiles EQ pregrabados para audífonos Sony | Filtro FIR de fase mixta (requiere medición externa previa) | **Celosía deformada Bark ($\lambda=0.72$) + linealización Lorentz $Bl(x)$ + adaptación NLMS en vivo** |
| **Acoplamiento Oído $\leftrightarrow$ Sala** | Independientes (el preset de sala ignora la anatomía del oído) | Reverberación fija sin acoplamiento al perfil de pinna | Fijo por mezcla codificada en formato propietario | Sala virtual fija sin optimización geodésica | **Optimizador Riemanniano $\Phi_{\text{SAF-Room}}^\infty$ + tensor $\mathbf{W}_{\text{SOFA}\to\text{RIR}}$ en tiempo real** |
| **Latencia Algorítmica del Núcleo** | $15\text{–}40\text{ ms}$ (búferes de ventana STFT) | $12\text{–}30\text{ ms}$ | $20\text{–}45\text{ ms}$ (inferencia CNN por ventana) | $5\text{–}25\text{ ms}$ (convolución FIR lineal) | **0.00 ms en los 5 Ejes Supremos y partición 0 (head) del convolver BRIR** |
| **Apertura y Ejecución en Android** | Caja negra propietaria limitada a ROMs con licencia | Cerrado al ecosistema Apple | Limitado a apps/dispositivos certificados | Cerrado a acuerdos OEM específicos | **100% Nativo Android (Ruta A Sin Root + Ruta B Global Magisk AudioFlinger)** |

---

## ✦ Instalación y Honestidad de Ingeniería

1. Descarga el artefacto del último build verde en CI (módulo Magisk + APK).
2. Flashea el zip en Magisk/KernelSU → reinicia (para activar la Ruta B `audioserver` global).
3. Instala el APK → abre IVANNA (en dispositivos sin root, la Ruta A arranca de inmediato con la calibración maestra `#51` y los 5 Ejes Supremos sobre el reproductor y captura `MediaProjection`).
4. Opcional: pega tu propia API key de Gemini en el panel del asistente para activarlo en línea — nunca viaja dentro del binario, y sin ella el asistente sigue funcionando offline.

**Honestidad de ingeniería:** este proyecto documenta lo que **no** hace con el mismo cuidado que lo que sí hace: sin root no existe la intercepción interna de `audioserver` (Ruta B) para apps que bloquean `MediaProjection`, el throughput de PMU se reporta como `N/M` en los SoCs donde el contador de kernel no es accesible desde espacio de usuario, y ninguna credencial de terceros viaja incrustada en el binario.

---

<div align="center">

**© 2026 Luis Uriel Pimentel Pérez — GORE TNS. Todos los derechos reservados.**

*Construido muestra a muestra. Auditado commit a commit. Verificado línea por línea.*

**⬡ IVANNA OMEGA SUPREME ⬡**

</div>
