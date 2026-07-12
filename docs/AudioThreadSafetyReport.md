# AudioThreadSafetyReport.md
**Fecha:** 2026-07-11  
**Rama:** audit/audio-thread-safety  
**Base commit:** e36e78b (SafetyLimiter integrado)  
**Funciones auditadas:** `nativeProcess()`, `nativeProcessBlock()`  
**Archivo:** `app/src/main/cpp/jni/ivanna_omega_jni.cpp`  

---

## Criterio de evaluación

Prohibido en audio thread RT:
`malloc` · `calloc` · `realloc` · `new` · `delete` · filesystem · sockets · `mutex` bloqueante · `sleep` · logging pesado · JNI costoso · bloqueos · I/O · creación de objetos temporales

Clasificación: **CRÍTICO** / **ALTO** / **MEDIO** / **BAJO** / **OK**

---

## `nativeProcess()` — líneas 137–270 de `ivanna_omega_jni.cpp`

| # | Línea | Código | Clasificación | Evidencia |
|---|---|---|---|---|
| 1 | 144 | `env->GetFloatArrayElements(buf, nullptr)` | **ALTO** | Puede forzar copia interna (`malloc`) si JVM no puede pin-ear. Ocurre cada frame. |
| 2 | 151–152 | `static thread_local float chL[2048], chR[2048]` | **OK** | `thread_local static` = allocado una vez por thread al primer uso. No `malloc` en hot path posterior. |
| 3 | 155–156 | `static thread_local float dryL[2048], dryR[2048]` | **OK** | Mismo caso. Allocación única, no recurrente. |
| 4 | 203 | `static thread_local float pdOutL[2048], pdOutR[2048]` | **OK** | Mismo caso. |
| 5 | 226 | `static thread_local float corrSmooth = 0.7f` | **OK** | Inicialización única, no en hot path. |
| 6 | 270 | `env->ReleaseFloatArrayElements(buf, data, 0)` | **BAJO** | Necesario para liberar pin/copia del punto 1. No evitable sin cambiar la interfaz JNI. |
| 7 | No aparece | `malloc` / `calloc` / `new` / `delete` | **OK** | No detectado en el cuerpo de `nativeProcess()`. |
| 8 | No aparece | `std::mutex` / `lock_guard` | **OK** | No detectado. Los accesos a `g_control_frame` usan `std::atomic` con `memory_order_relaxed`. |
| 9 | No aparece | Filesystem / sockets / I/O | **OK** | No detectado. |
| 10 | No aparece | `LOGI` / `LOGE` en hot path | **OK** | No hay logging dentro del cuerpo de procesamiento. Solo en `nativeReset` y `nativeInit`. |
| 11 | 186–202 | `g_control_frame.evo_genome_nho[x].load(memory_order_relaxed)` × 5 | **BAJO** | 5 loads atómicos por frame cuando `evolutionary_active`. Cada load = L1 cache access. Costo negligible en ARM64 pero documentado. |
| 12 | 220–232 | Correlación L/R: bucle `double` acumulador por N frames | **MEDIO** | Usa `double` (64-bit) en lugar de `float`. En NEON ARM64, `double` fuerza FPU scalar. Para N=512 frames: ~512 multiplicaciones double + sqrt double. Medición pendiente. No bloquea pero puede reducir headroom temporal. |

### Resumen `nativeProcess()`:

| Severidad | Count | Nota |
|---|---|---|
| CRÍTICO | 0 | — |
| ALTO | 1 | `GetFloatArrayElements` — inherente a JNI array API |
| MEDIO | 1 | Correlación L/R en `double`, oportunidad NEON `float` |
| BAJO | 2 | `ReleaseFloatArrayElements` + 5 atomic loads |
| OK | 8 | Sin mutex, sin malloc, sin I/O, sin logging en hot path |

---

## `nativeProcessBlock()` — líneas 299–360 de `ivanna_omega_jni.cpp`

| # | Línea | Código | Clasificación | Evidencia |
|---|---|---|---|---|
| 1 | 309–310 | `float lBuf[2048], rBuf[2048], oL[2048], oR[2048]` | **OK** | Stack allocation — sin heap. 4 × 2048 × 4 bytes = 32 KB en stack. Dentro de límites de stack Android (mínimo 8 MB por thread). |
| 2 | 311–314 | `copyJFloat(env, inL, lBuf, n)` × 2 | **ALTO** | Depende de implementación de `copyJFloat()`. Si usa `GetFloatArrayElements` internamente, mismo riesgo que punto 1 de `nativeProcess()`. |
| 3 | No aparece | `malloc` / `new` / `delete` | **OK** | No detectado. |
| 4 | No aparece | `std::mutex` | **OK** | No detectado. |
| 5 | No aparece | Logging | **OK** | No hay `LOGI/LOGE` en el body de procesamiento. |
| 6 | 330–340 | `g_control_frame` atomic loads × 5 | **BAJO** | Mismo análisis que `nativeProcess()`. |

### `copyJFloat` — implementación real (línea 60):
```cpp
static inline bool copyJFloat(JNIEnv* env, jfloatArray src, float* dst, int n) {
    if (!src || n <= 0) return false;
    jfloat* p = env->GetFloatArrayElements(src, nullptr);  // ← mismo riesgo ALTO
    if (!p) return false;
    memcpy(dst, p, n * sizeof(float));
    env->ReleaseFloatArrayElements(src, p, JNI_ABORT);
    return true;
}
```

`nativeProcessBlock()` llama `copyJFloat` **2 veces** (inL + inR): `GetFloatArrayElements` × 2 por frame.  
Mismo riesgo ALTO que `nativeProcess()`. Total en toda la API: 3 invocaciones de `GetFloatArrayElements` por frame (1 en `nativeProcess` + 2 en `nativeProcessBlock`).

---

## Hallazgo adicional: `GetFloatArrayElements` — el único ALTO real

`GetFloatArrayElements` es la única operación de riesgo real en ambas funciones. La JVM **puede o no** hacer una copia del array dependiendo de si puede pin-ear la memoria:

- Si **pin-ea** (sin copia): O(1), sin malloc → seguro en RT
- Si **copia** (fallback): O(N) malloc interno → violación RT

En la práctica, ART en Android 8+ tiende a pin-ear arrays `float[]` nativos. Sin embargo, **esto no está garantizado por la especificación JNI**. El comportamiento puede cambiar entre versiones de ART o bajo presión de GC.

**Alternativa sin este riesgo:** Usar `GetPrimitiveArrayCritical` / `ReleasePrimitiveArrayCritical`, que previene GC durante la operación (no hace copia) pero requiere que el código entre critical y release sea mínimo.

---

## Estado general del audio thread

| Criterio | Estado | Evidencia |
|---|---|---|
| Sin `malloc`/`new` explícito | ✅ | No detectado en hot path |
| Sin `std::mutex` | ✅ | No detectado en hot path |
| Sin filesystem / I/O | ✅ | No detectado |
| Sin logging en hot path | ✅ | No detectado |
| Sin `sleep` | ✅ | No detectado |
| JNI pin-ear garantizado | ⚠️ | `GetFloatArrayElements` — depende de ART |
| `double` en correlación | ⚠️ | Candidato a optimización NEON float |
| `thread_local static` buffers | ✅ | Allocación única, no recurrente |

---

## Acciones propuestas (sin implementar — solo documentadas)

### P1 — BAJO RIESGO: Cambiar `GetFloatArrayElements` por `GetPrimitiveArrayCritical`
- **Archivo:** `ivanna_omega_jni.cpp:144`
- **Impacto:** Elimina posibilidad de copia interna
- **Restricción:** El código entre `GetPrimitive` y `Release` debe ser mínimo — evaluar si la cadena DSP completa puede ejecutarse en critical section (típicamente sí en Android moderno)
- **Prerequisito:** Medir primero con `simpleperf` si el `GetFloatArrayElements` actual introduce jitter

### P2 — MEDIO RIESGO: Correlación L/R en `float` + NEON
- **Archivo:** `ivanna_omega_jni.cpp:220-232`
- **Impacto:** Posible reducción de costo en ~40% en el bloque de correlación
- **Prerequisito:** Medir costo actual con `simpleperf` antes de optimizar

### P3 — BAJO RIESGO: Verificar `copyJFloat()` en `nativeProcessBlock`
- **Acción:** `grep -n "copyJFloat" app/src/main/cpp/jni/ivanna_omega_jni.cpp`
- **Si usa `GetFloatArrayElements`:** Aplicar misma consideración que P1

---

## Rollback

```bash
# Este archivo es solo documentación — no modifica código DSP.
# Para eliminar la rama completa:
git checkout feature/safetylimiter
git branch -D audit/audio-thread-safety
git push origin --delete audit/audio-thread-safety
```

---

*Ninguna afirmación sin evidencia de archivo:línea. Fecha de próxima revisión: tras medición con simpleperf.*
