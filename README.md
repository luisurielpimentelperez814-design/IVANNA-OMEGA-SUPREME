# IVANNA OMEGA SUPREME

Motor de procesamiento de audio DSP + neuromorphic para Android, con integración Magisk para procesamiento a nivel de sistema.

---

## Qué hace realmente

**Sin root (app sola):**
- Procesa audio de archivos locales vía `IvannaBridgePlayer` (decodifica → DSPBridge → AudioTrack)
- Captura audio del sistema vía `MediaProjection` (con permiso del usuario)
- Aplica toda la cadena DSP en tiempo real

**Con root + módulo Magisk:**
- Inyecta `libomega_effect.so` en el HAL de Android como `AudioEffect` global
- El daemon recibe comandos de la app vía socket Unix (`/dev/socket/ivanna_omega`)
- El monitor `mqa_monitor.sh` detecta la app activa y aplica el preset correcto automáticamente
- El daemon corre con `SCHED_FIFO` prioridad 98 en los big cores del SoC

## Cadena DSP

```
Input
  └── ParametricEQ (3 bandas + presence)
  └── Compressor (threshold/ratio adaptativo)
  └── HarmonicExciter (2x oversampling anti-aliasing)
  └── StereoWidener (crossover mono-safe)
  └── PDEngine
        ├── NHO (generador armónico neuromorphic)
        ├── Spatial (HRTF binaural)
        ├── HRTF (Head-Related Transfer Function)
        └── Evolutionary Kernel (AG modula NHO+Spatial en vivo)
  └── NPE (Neuromorphic Processing Engine)
        ├── Inhibición lateral
        ├── Compresión OHC coclear
        └── AGC adaptativo
Output
```

## Instalación

### App (sin root)

```bash
git clone https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME.git
cd IVANNA-OMEGA-SUPREME
./gradlew assembleDebug
adb install app/build/outputs/apk/debug/app-debug.apk
```

### Módulo Magisk (requiere root + Magisk ≥ v20)

1. Compilar el proyecto (genera `libomega_effect.so` en `app/build/`)
2. Copiar el `.so` al módulo:
   ```bash
   cp app/build/intermediates/stripped_native_libs/debug/*/lib/arm64-v8a/libivanna_omega.so \
      magisk_module/system/vendor/lib64/soundfx/libomega_effect.so
   ```
3. Zipar el módulo:
   ```bash
   cd magisk_module && zip -r ../ivanna_omega_magisk.zip .
   ```
4. Instalar desde Magisk Manager → Módulos → Instalar desde almacenamiento
5. Reiniciar

### Control desde ADB (con módulo instalado)

```bash
# Estado del daemon
adb shell su -c "/system/bin/ivanna_control.sh status"

# Cambiar preset
adb shell su -c "/system/bin/ivanna_control.sh preset Spatial"

# Modo Concierto
adb shell su -c "/system/bin/ivanna_control.sh concert on"

# Telemetría en tiempo real
adb shell su -c "/system/bin/ivanna_control.sh telemetry"
```

## Perfiles de audio

| Perfil | Uso óptimo |
|--------|-----------|
| `Flat` | Referencia / mezcla |
| `Warm` | Jazz, acústico, voces |
| `Bright` | Pop, electrónica, claridad |
| `Punch` | Hip-hop, rap, impacto |
| `Spatial` | Binaural, cine, VR |
| `Heavy` | Metal, distorsión |
| `Vocal` | Podcast, podcast, speech |
| `Bass` | EDM, techno, bass |

El monitor `mqa_monitor.sh` selecciona el perfil automáticamente según la app activa:
- **Tidal / Qobuz / Amazon Music HD** → `Flat` (no colorear lossless)
- **Spotify / YouTube Music** → `Warm` (compensar compresión lossy)
- **YouTube / video** → `Spatial`
- **Juegos** → `Punch`

## Arquitectura

```
App (Kotlin/Compose)
  ├── IvannaControlPanel        — UI de controles
  ├── DSPBridge                 — JNI → libivanna_omega.so
  ├── IvannaBridgePlayer        — Reproductor propio (archivos locales)
  ├── OmegaEngineBridge         — Socket → daemon Magisk
  ├── MagiskBridge              — API de alto nivel para el módulo
  ├── LearningBias              — Sesgo aprendido por contexto
  ├── UserProfileManager        — Perfiles de usuario persistentes
  └── ConcertMode               — Modo Concierto (Spatial + reverb)

M�dulo Magisk
  ├── customize.sh              — Instalador (valida ELF, fusiona audio_effects.xml)
  ├── post-fs-data.sh           — Anti-bootloop + setprop de estado
  ├── service.sh                — Daemon real-time + monitor MQA
  ├── ivanna_control.sh         — CLI de control via socket
  ├── mqa_monitor.sh            — Detector de app activa → preset automático
  └── concert_mode.sh           — Activador de Modo Concierto

C++ Native (libivanna_omega.so)
  ├── HarmonicExciter           — Exciter con anti-aliasing 2x OS
  ├── ParametricEQ              — EQ paramétrico 3 bandas + presence
  ├── Compressor                — Compresor dinámico
  ├── StereoWidener             — Ensanchamiento estéreo
  ├── PDEngine                  — NHO + Spatial + HRTF + Evolutivo
  └── ivanna_npe                — Motor neuromorphic (NPE)
```

## Requisitos

- Android 9 (API 28) o superior
- Arquitectura: arm64-v8a
- Para módulo Magisk: root + Magisk ≥ v20

## Notas técnicas

- El pipeline C++ procesa audio en estéreo intercalado [L0,R0,L1,R1,...] — correctamente de-intercalado antes de la cadena DSP
- `HarmonicExciter` usa oversampling 2x con LPF anti-aliasing @ 10.8kHz para evitar aliasing
- El `EvolutionaryKernel` corre en hilo separado y modula NHO+Spatial vía genoma en tiempo real
- `LearningBias` acumula correcciones del usuario por (contexto, parámetro) y las aplica como sesgo
- El daemon Magisk usa `SCHED_FIFO` prioridad 98 y se ancla a los big cores del SoC

## Estado del proyecto

- [x] Cadena DSP completa en C++
- [x] UI Compose con todos los controles
- [x] Módulo Magisk con efecto global
- [x] Daemon con socket de control
- [x] Monitor automático de app activa
- [x] Perfiles de usuario con aprendizaje
- [x] Modo Concierto
- [x] Anti-aliasing en HarmonicExciter
- [ ] Convolución de sala real (IR loading)
- [ ] Sincronización de perfiles en la nube
- [ ] Soporte USB DAC dedicado

