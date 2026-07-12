# SYSTEM_AUDIO_CAPTURE_AUDIT.md
**Fecha:** 2026-07-11  
**Rama:** audit/audio-thread-safety  
**Auditor:** IVANNA R&D Consortium  

---

## 1. Propósito original

`SystemAudioCapture` fue diseñado para capturar audio del sistema vía `MediaProjection` + `AudioRecord` (modo `PLAYBACK_CAPTURE`), alimentar un buffer circular nativo con `nativeFeedBuffer()`, y calcular métricas de nivel (RMS/Peak en dBFS) en tiempo real.

Archivos involucrados:
- `app/src/main/java/com/ivanna/omega/audio/SystemAudioCapture.kt` — singleton Kotlin con `getInstance()`
- `app/src/main/cpp/system_audio_capture.cpp` — 91 líneas de JNI con buffer circular y métricas

---

## 2. Evidencia de uso / no uso

### Comando ejecutado:
```bash
grep -rn "SystemAudioCapture" app/src/main/java/ --include="*.kt" | grep -v "SystemAudioCapture.kt"
```

### Resultado:
```
(vacío — exit code 1)
```

**Ningún archivo Kotlin fuera de `SystemAudioCapture.kt` importa ni referencia esta clase.**

### Confirmación adicional:
```bash
grep -rn "SystemAudioCapture.getInstance" . --include="*.kt"
```
```
(vacío)
```

**Clasificación: 💀 HUÉRFANO**  
La clase existe y compila. Tiene punto de entrada (`getInstance()`). Pero ningún flujo de ejecución la invoca — nunca se llama `getInstance()` desde ningún otro archivo.

---

## 3. Riesgos encontrados (evidencia directa de código)

### RIESGO 1 — CRÍTICO: Mismatch de firma JNI

| | Kotlin (`SystemAudioCapture.kt:87`) | C++ (`system_audio_capture.cpp:29`) |
|---|---|---|
| Firma declarada | `nativeFeedBuffer(buffer: ByteBuffer, size: Int)` | `nativeFeedBuffer(JNIEnv*, jobject, jfloatArray audio_data, jint length)` |
| Tipo del buffer | `ByteBuffer` (Java NIO) | `jfloatArray` (array JNI de float) |

Si esta función fuera llamada, lanzaría `UnsatisfiedLinkError` en runtime — los tipos de parámetro no coinciden con la firma JNI mangled esperada.

### RIESGO 2 — ALTO: `std::mutex` bloqueante en ruta potencialmente llamable desde audio thread

```cpp
// system_audio_capture.cpp:36
std::lock_guard<std::mutex> lock(g_buffer_mutex);
```

Si `nativeFeedBuffer()` fuera llamada desde el callback de audio (lo que su diseño implica — recibe PCM en tiempo real), un `std::mutex` bloqueante viola las reglas del audio thread RT. Un `std::mutex` puede invocar syscalls y causar priority inversion.

### RIESGO 3 — ALTO: `GetFloatArrayElements` + `ReleaseFloatArrayElements` en audio thread

```cpp
// system_audio_capture.cpp:33
jfloat* data = env->GetFloatArrayElements(audio_data, nullptr);
// ...
// system_audio_capture.cpp:65
env->ReleaseFloatArrayElements(audio_data, data, JNI_ABORT);
```

`GetFloatArrayElements` puede forzar una copia del array (si la JVM no puede pin-ear el array directamente), lo que involucra `malloc()` interno. Prohibido en audio thread RT.

### RIESGO 4 — MEDIO: Log en ruta hot por cada buffer

```cpp
// system_audio_capture.cpp:64
LOGI("nativeFeedBuffer: %d samples, RMS=%.1f dB, Peak=%.1f dB", length, rms_db, peak_db);
```

`__android_log_print()` realiza I/O de sistema. Prohibido en audio thread. Si el módulo fuera activado, añadiría latencia no determinista en cada bloque.

### RIESGO 5 — MEDIO: Buffer circular de 65536 floats sin protección de overflow

```cpp
// system_audio_capture.cpp:17-22
static constexpr int BUFFER_SIZE = 65536;
static float g_audio_buffer[BUFFER_SIZE];  // 256 KB estático global
static std::atomic<int> g_write_pos{0};
static std::atomic<int> g_read_pos{0};
static std::mutex g_buffer_mutex;
```

El write_pos avanza con módulo pero la lectura no está implementada (`nativeHasData()` existe, pero no hay `nativeReadBuffer()`). El buffer se llena y sobrescribe sin control de overrun.

---

## 4. Decisión arquitectónica

**CONSERVAR, NO ELIMINAR. Mover a `/experimental/system_capture/`.**

Razones:
1. El diseño tiene valor potencial para IvannaLab (análisis de señal off-thread).
2. Los riesgos son todos de "si se activa en audio thread" — actualmente nunca se llama.
3. El mismatch de firma JNI es fácil de corregir cuando se rehabilite.
4. La infraestructura de buffer circular + métricas RMS/Peak es reutilizable.

**Condición para rehabilitación:** Debe ser llamado exclusivamente desde hilo de análisis (no RT), corregir la firma JNI, reemplazar `std::mutex` por `std::atomic` o lock-free ring, y eliminar el `LOGI` del hot path.

---

## 5. Instrucciones de rollback

```bash
# Revertir movimiento experimental:
git checkout feature/safetylimiter -- app/src/main/java/com/ivanna/omega/audio/SystemAudioCapture.kt
git checkout feature/safetylimiter -- app/src/main/cpp/system_audio_capture.cpp

# O revertir toda la rama:
git checkout feature/safetylimiter
git branch -D audit/audio-thread-safety
```

---

*Autoridad: código real en repositorio. Ninguna afirmación sin evidencia de archivo:línea.*
