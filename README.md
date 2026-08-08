# IVANNA OMEGA SUPREME v9.1

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
│  │   Modo A — JSON command (OmegaEngineBridge):         │  │
│  │     recv(5ms timeout) → CommandServer::              │  │
│  │       handleJsonCommand() → respuesta JSON           │  │
│  │                                                      │  │
│  │   Modo B — SHM fd delivery (ShmManager.kt):          │  │
│  │     timeout expira sin datos → sendmsg(SCM_RIGHTS)   │  │
│  │     entrega el fd del SHM mapeado                    │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                             │
│  ┌──────────────────────────────────────────────────────┐  │
│  │   @omega_command_socket  — control loop              │  │
│  │   CommandServer::acceptLoop() en hilo separado       │  │
│  │   Mismo dispatch JSON + notificación SHM 12 bytes    │  │
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
| `@omega_daemon_socket` | Abstract | Socket principal. Demux automático: JSON command si el cliente envía datos en ≤5ms; SCM_RIGHTS (fd SHM) si no hay datos. |
| `@omega_command_socket` | Abstract | Loop de control paralelo. Mismo protocolo JSON + notificación SHM. |

Los sockets **abstract** de Linux no crean archivos en el filesystem —
no hay ruta `/dev/socket/` ni `/data/` que comprobar. El daemon se detecta
via `persist.ivanna.daemon_active` (SystemProperty) o con un connect/close
de prueba a `@omega_daemon_socket`.

---

## Protocolo JSON

Todos los comandos son UTF-8, sin longitud prefijada, sobre `SOCK_STREAM`.

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
adb shell tail -f /data/adb/ivanna_omega/daemon.log
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
├── module.prop           — id, version, author
├── service.sh            — lanza ivanna_daemon en boot (watchdog loop)
├── customize.sh          — instalador
├── system/bin/
│   └── ivanna_daemon     — binario PIE ARM64 (build desde daemon/CMakeLists.txt)
├── vendor_base/          — HAL hooks
└── sepolicy/             — reglas SELinux para el socket abstracto
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

**v9.1** — Fix crítico daemon/socket
- `CommandServer::acceptLoop()` ahora lee el payload JSON antes de responder
  (fix: antes enviaba 12 bytes SHM sin nunca leer el comando del cliente).
- `@omega_daemon_socket` implementa demux automático: JSON command o SCM_RIGHTS.
- `OmegaDspState` y `handleJsonCommand()` integrados en `command_server.cpp`
  (antes vivían en `OmegaDaemonV8.cpp`, archivo sin CMakeLists — código muerto).
- `OmegaShmManager::init()` unificado: `ivanna_daemon.cpp` usa el mismo fd
  que `CommandServer`, sin re-abrir el archivo backing.
