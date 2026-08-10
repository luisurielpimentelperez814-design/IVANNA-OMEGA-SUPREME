# IVANNA OMEGA SUPREME v9.2

**Universal Audio Immersive Renderer — Android (Magisk/Root & Non-Root)**

Motor de audio espacial ultra-bajo latencia (<5ms) para Android, con DSP system-wide
vía módulo Magisk, IPC basado en Unix abstract sockets y memoria compartida con
protocolo seqlock.

---

## Arquitectura

```
┌─────────────────────────────────────────────────────────────┐
│                     App Kotlin (UI + JNI)                   │
│                                                             │
│  OmegaEngineBridge ──JSON──► @omega_daemon_socket          │
│  MagiskBridge      ──TEXT──► @omega_daemon_socket          │
│  ShmManager.kt    ◄──fd────  SCM_RIGHTS                    │
│  MagiskBridge     ──prop──►  SystemProperties               │
└───────────────────────────────┬─────────────────────────────┘
                                │ AF_UNIX abstract namespace
                                ▼
┌─────────────────────────────────────────────────────────────┐
│            ivanna_daemon  (PIE, ARM64, Magisk/system/bin)   │
│                                                             │
│  ┌──────────────────────────────────────────────────────┐  │
│  │   @omega_daemon_socket  — socket principal           │  │
│  │                                                      │  │
│  │   Demux por primer carácter no-espacio del payload:  │  │
│  │                                                      │  │
│  │   Modo A  — JSON '{' (OmegaEngineBridge):            │  │
│  │     handleJsonCommand() → respuesta JSON             │  │
│  │                                                      │  │
│  │   Modo A2 — Texto plano (MagiskBridge):              │  │
│  │     handleTextCommand() → respuesta texto            │  │
│  │     SET_PF_*, STATUS, GET_TELEMETRY, SET_BYPASS...   │  │
│  │                                                      │  │
│  │   Modo B  — Sin datos en 5ms (ShmManager.kt):        │  │
│  │     sendmsg(SCM_RIGHTS) — entrega fd del SHM         │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                             │
│  ┌──────────────────────────────────────────────────────┐  │
│  │   @omega_command_socket  — control loop              │  │
│  │   CommandServer::acceptLoop() en hilo separado       │  │
│  │   Mismo demux JSON/TEXT + notificación SHM 12 bytes  │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                             │
│  ┌──────────────────────────────────────────────────────┐  │
│  │   OmegaShmManager  — región SHM seqlock              │  │
│  │   /data/adb/ivanna_omega/omega_shm                   │  │
│  │   64 KiB · mlock() · MAP_SHARED                      │  │
│  │   Header: atomic<uint64_t> epoch + uint32_t len      │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

---

## Sockets

| Socket | Namespace | Propósito |
|--------|-----------|-----------|
| `@omega_daemon_socket` | Abstract | Socket principal. Demux automático: JSON command (OmegaEngineBridge), texto plano (MagiskBridge), o SCM_RIGHTS (fd SHM) si no hay datos en 5ms. |
| `@omega_command_socket` | Abstract | Loop de control paralelo (hilo separado). Mismo protocolo demux. |

Los sockets **abstract** de Linux no crean archivos en el filesystem —
no hay ruta `/dev/socket/` ni `/data/` que comprobar. El daemon se detecta
via `persist.ivanna.daemon_active` (SystemProperty) o con un connect/close
de prueba a `@omega_daemon_socket`.

---

## Protocolo — JSON (OmegaEngineBridge)

Todos los comandos son UTF-8, sin longitud prefijada, sobre `SOCK_STREAM`.
El demux detecta JSON por primer carácter `{`.

### Comandos soportados

| `action` | Campos | Respuesta |
|----------|--------|-----------|
| `SET_EQ_BANDS` | `gains[10]`, `listenPhon`, `refPhon` | `{ok, gains[10], listenPhon, refPhon}` |
| `SET_PERCEPTUAL_STATE` | `compressor`, `exciterReduction`, `highCutHz`, `spatialWidth`, `loudnessTargetLuFS`, `harmonicGain`, `antiDolbyIntensity` | `{ok}` |
| `SET_INTENSITY` | `intensity` [0–1] | `{ok, intensity}` |
| `SET_PF_PARAMS` | `params[13]` | `{ok}` |
| `SET_ADAPTIVE_STATE` | `targetGain`, `compAmount`, `excRed` | `{ok}` |
| `SET_YAMNET_SCORES` | `speech`, `music`, `classId`, `confidence` | `{ok}` |
| `SET_ROUTE_PROFILE` | `bassBoostDb`, `dialogBoostDb`, `widenerMult` | `{ok}` |
| `SET_SAF_STATE` | `deltaEnergy`, `metricNorm`, `memory`, `gain` | `{ok}` |
| `PING` | — | `{ok, pong:true, uptime_ms}` |
| `GET_STATUS` | — | estado DSP completo |

### Ejemplo

```json
// Request
{"action":"SET_INTENSITY","intensity":0.85}

// Response
{"ok":true,"intensity":0.850}
```

---

## Protocolo — Texto plano (MagiskBridge)

`MagiskBridge.sendCommand()` envía comandos de texto plano con formato
`VERB:value\n` o `VERB\n`. El demux detecta el protocolo por ausencia de `{`.

| Comando | Formato | Respuesta |
|---------|---------|-----------|
| `STATUS` | `STATUS\n` | `IVANNA-OMEGA OK intensity=0.850 bypass=0 daemon=active` |
| `GET_TELEMETRY` | `GET_TELEMETRY\n` | `temp=0.0 latency=<ms> uptime_ms=<ms> intensity=<v>` |
| `RELOAD_PARAMS` | `RELOAD_PARAMS\n` | `ACK RELOAD_PARAMS` |
| `SET_BYPASS` | `SET_BYPASS:0\n` | `ACK SET_BYPASS:0` |
| `SET_PRESET` | `SET_PRESET:Spatial\n` | `ACK SET_PRESET:Spatial` |
| `SET_REVERB` | `SET_REVERB:0.7\n` | `ACK SET_REVERB:0.700` |
| `SET_PF_DRIVE` | `SET_PF_DRIVE:0.5\n` | `ACK SET_PF_DRIVE:0.5000` |
| `SET_PF_WET` | `SET_PF_WET:0.8\n` | `ACK SET_PF_WET:0.8000` |
| `SET_PF_*` | `SET_PF_<PARAM>:<v>\n` | `ACK SET_PF_<PARAM>:<v>` |

**Índices PF Engine en `pf_params[13]`:**

| Idx | Nombre | Comando |
|-----|--------|---------|
| 0 | drive | `SET_PF_DRIVE` |
| 1 | wet | `SET_PF_WET` |
| 2 | mix | `SET_PF_MIX` |
| 3 | alpha | `SET_PF_ALPHA` |
| 4 | beta | `SET_PF_BETA` |
| 5 | gamma | `SET_PF_GAMMA` |
| 6 | freq | `SET_PF_FREQ` |
| 7 | resonance | `SET_PF_RESONANCE` |
| 8 | low | `SET_PF_LOW` |
| 9 | mid | `SET_PF_MID` |
| 10 | high | `SET_PF_HIGH` |
| 11 | presence | `SET_PF_PRESENCE` |
| 12 | master | `SET_PF_MASTER` |

---

## Memoria Compartida (SHM)

El daemon crea `/data/adb/ivanna_omega/omega_shm` (64 KiB, `MAP_SHARED`,
bloqueado con `mlock()`). La app recibe el fd vía `SCM_RIGHTS` y lo mapea
con `android.os.SharedMemory` (API 27+).

### Layout del buffer

```
Offset  Size  Campo
0       8     epoch  (atomic<uint64_t>, seqlock — par=estable, impar=escribiendo)
8       4     frame_len (uint32_t, bytes del frame actual)
12      4     reserved
16      N     payload (UnifiedControlFrame serializado)
```

**Protocolo seqlock de lectura:**
1. Leer `epoch` → si impar, reintentar.
2. Copiar datos.
3. Leer `epoch` de nuevo → si cambió, reintentar.

---

## Inicio del daemon

El daemon lo lanza Magisk automáticamente en boot via `service.sh`:

```sh
ivanna_daemon --socket "@omega_daemon_socket" [--rate 48000] [--buffer 64] [--realtime]
```

Para ver logs en tiempo real:
```sh
adb shell tail -f /data/adb/ivanna_daemon.log
adb logcat -s IVANNA_OMEGA_DAEMON IVANNA_CMD IVANNA_SHM
```

---

## Stack técnico

| Capa | Tecnología |
|------|------------|
| DSP nativo | C++17, ARM64 NEON, SCHED_FIFO |
| IPC | AF_UNIX abstract socket + SCM_RIGHTS |
| SHM | mmap MAP_SHARED + mlock + seqlock |
| Motor espacial | VBAP, HRTF bilineal, SAF Φ∞ |
| Clasificador audio | TinyML, 64-Band Mel-STFT, ConvNeXt |
| EQ perceptual | Curvas ISO 226, 10 bandas |
| App | Kotlin, Jetpack Compose, Android API 35 |
| Empaquetado | Módulo Magisk v2 |

---

## Módulo Magisk

```
magisk_module/
├── META-INF/com/google/android/update-binary
├── module.prop           — id, version, author, updateJson (Foco #8: apunta a
│                           magisk_module/update.json, no a la raíz)
├── update.json           — fuente real del update-checker (Foco #8)
├── service.sh            — lanza ivanna_daemon en boot (watchdog loop, MQA_PID
│                           vía PID file — Foco #2)
├── customize.sh          — instalador; aplica magisk_module/sepolicy.rule live
├── sepolicy.rule         — reglas SELinux reales para el socket abstracto
│                           (untrusted_app/isolated_app → su/magisk connectto).
│                           NO confundir con /sepolicy/sepolicy.rule en la raíz
│                           del repo — ese es legado de una arquitectura anterior
│                           (efecto AudioFlinger INSERT, dominio omega_daemon
│                           propio) y no lo lee nada en el árbol actual.
├── system/bin/
│   └── ivanna_daemon     — binario PIE ARM64 (build desde daemon/CMakeLists.txt)
└── vendor_base/          — HAL hooks
```

---

## Build

```bash
# Desde Android Studio o con Gradle:
./gradlew assembleDebug

# El daemon se compila como target separado en:
# app/src/main/cpp/daemon/CMakeLists.txt
# y se stagea automáticamente a magisk_module/system/bin/ivanna_daemon
```

---

## Changelog reciente

Consulta [CHANGELOG.md](CHANGELOG.md) para el historial completo.

**v9.2** — Demux texto plano + fix "queued"
- `handleTextCommand()` agregado a `CommandServer`: maneja los comandos de texto plano
  enviados por `MagiskBridge.sendCommand()` (`STATUS`, `GET_TELEMETRY`, `SET_PF_*`,
  `SET_BYPASS`, `SET_PRESET`, `SET_REVERB`, `RELOAD_PARAMS`).
- Demux automático en `@omega_daemon_socket` y `@omega_command_socket` por primer
  carácter del payload: `{` → JSON dispatch, otro → texto plano dispatch.
- Fix root: cuando el daemon estaba activo pero `MagiskBridge` enviaba comandos
  de texto plano, el daemon respondía `{"ok":false,"error":"no action field"}` y
  `sendCommand()` registraba `"queued"` en logcat. Con `handleTextCommand()` los
  comandos se procesan y responden correctamente.

**v9.1** — Fix crítico daemon/socket
- `CommandServer::acceptLoop()` ahora lee el payload antes de responder.
- `@omega_daemon_socket` implementa demux automático JSON/SCM_RIGHTS.
- `OmegaDspState` y `handleJsonCommand()` integrados en `command_server.cpp`.
- `OmegaShmManager::init()` unificado.
