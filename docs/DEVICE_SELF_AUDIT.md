# Auto-Auditoría y Auto-Optimización de Dispositivo — IVANNA

> Desde la primera instalación en CUALQUIER equipo, IVANNA sola analiza
> dónde está instalada, audita el dispositivo, formula mejoras y las aplica
> en caliente — con rollback automático ante cualquier regresión medida.

## Arquitectura

```
Primera instalación (o bajo demanda)
        │
        ▼
┌──────────────────┐   APIs públicas + /proc + /sys (solo lectura)
│ DeviceProfiler   │ ──▶ DeviceProfile (JSON + fingerprint)
└────────┬─────────┘
         ▼
┌──────────────────┐   22 SoCs + genérico por familia
│ HardwareKnow-    │ ──▶ Match (EXACT_SOC / CODENAME / FAMILY / GENERIC)
│ ledgeBase        │
└────────┬─────────┘
         ▼
┌──────────────────┐   Lógica pura (testeable en JVM)
│ DeviceAuditor    │ ──▶ AuditReport (cuellos de botella + oportunidades)
└────────┬─────────┘
         ▼
┌──────────────────┐   Registro cerrado de 7 optimizaciones
│ HotOptimizer     │ ──▶ ApplyResult por paso (APPLIED / ALREADY_OPTIMAL /
└────────┬─────────┘   UNAVAILABLE / FAILED / REVERTED)
         ▼
┌──────────────────┐
│ FirstRunSelfAudit│ ──▶ Resumen + persistencia (SharedPreferences)
└──────────────────┘
```

Archivos (paquete nuevo `com.ivanna.omega.device`, sin tocar código existente):

| Archivo | Qué es |
|---|---|
| `device/DeviceProfile.kt` | Modelo de datos puro + fingerprint SHA-256 + JSON mínimo. |
| `device/DeviceProfiler.kt` | Recolección (Build, AudioManager, HardwareProbe, /proc/cpuinfo, /sys cpufreq, /vendor/lib64/hw, /sys/fs/selinux, /data/adb/modules). |
| `device/HardwareKnowledgeBase.kt` | 22 SoCs + genérico ARM64, con fuente por dato. |
| `device/DeviceAuditor.kt` | Comparación perfil vs base → informe. |
| `device/HotOptimizer.kt` | Registro cerrado de optimizaciones + rollback. |
| `device/FirstRunSelfAudit.kt` | Orquestador + persistencia + punto de integración. |
| `cpp/device_cpu_profiler.hpp` | Perfilador nativo header-only (SIMD por getauxval, topología, afinidad). |
| `cpp/tests/test_device_cpu_profiler.cpp` | Test standalone (main propio, 27 checks). |
| `test/.../device/*.kt` | 4 archivos de tests JVM (37 tests). |

## Flujo de primera instalación

1. `IVANNAApplication.onCreate()` → `FirstRunSelfAudit.runIfNeeded(this)` (ver punto de integración abajo).
2. El perfilador recolecta el perfil. Todo lo que no se puede determinar queda en `null` ("no detectado"), nunca inventado.
3. El auditor resuelve el SoC en la base y emite cuellos de botella + oportunidades, cada una con su valor esperado y su fuente (medido o estimado).
4. El optimizador formula el plan (de menor a mayor riesgo) y aplica lo aplicable en caliente. Cada paso se verifica: si la latencia sube > 5 ms o los underruns suben, se revierte automáticamente al valor anterior.
5. Se guarda el perfil (fingerprint + resumen) en `ivanna_device_self_audit`. Si la base de conocimiento no cambió, no se repite el trabajo.

### Punto de integración

Archivo: `app/src/main/java/com/ivanna/omega/core/IVANNAApplication.kt`
(en `onCreate()`, DESPUÉS de IntegrityGate y de `IvannaControlLoop.start()`,
JUNTO al arranque de `SafmusHardwareCoordinator`, hoy ~línea 334):

```kotlin
runCatching { com.ivanna.omega.device.FirstRunSelfAudit.runIfNeeded(this) }
    .onFailure { Log.w(TAG, "Auto-auditoría de dispositivo falló (no fatal): ${it.message}") }
```

No se llama antes de `super.onCreate()` ni antes de IntegrityGate: el gate
decide primero si el DSP puede arrancar (fail-closed), y la auditoría es de
solo lectura + optimizaciones fail-closed, así que no puede romper el arranque.

## Base de conocimiento: qué SoC cubre y con qué fuente

22 SoCs + 1 perfil genérico. Fuente por dato (`EvidenceKind`, misma
taxonomía que `SafmusUniversalKernel`):

| Familia | SoCs | Fuente de núcleos/frecuencias/DSP |
|---|---|---|
| Qualcomm Snapdragon 8 | 8 Gen 3, 8 Gen 2, 8 Gen 1, 888, 870, 865 | VENDOR_DOC (hoja pública) |
| Qualcomm Snapdragon 7/6 | 7 Gen 3, 7+ Gen 2, 7 Gen 1, 778G, 6 Gen 1, 695, **6s Gen 3** | VENDOR_DOC, salvo 6s Gen 3 = **MEASURED** |
| MediaTek | Dimensity 9000, 8000, 8200, 7050, Helio G99 | VENDOR_DOC |
| Samsung Exynos | 2100, 2200 | VENDOR_DOC |
| Google Tensor | G2, G3 (desempatados por codename: cloudripper/zuma) | VENDOR_DOC |
| HiSilicon Kirin | 9000 | VENDOR_DOC |
| Genérico | ARM64 desconocido | UNKNOWN + medición en vivo |

El Moto G85 es el único con datos **medidos** (deriva −6.25 ppm, latencia
AudioFlinger 21.00 ms, 199 underruns / 523 overruns, HAL
`audio.primary.blair.so`). Las configuraciones recomendadas (búfer, hilos,
latencia esperada) son **ESTIMATED** en todos los SoCs salvo el G85: son
reglas derivadas de la clase de SoC, no mediciones por dispositivo. La base
lo dice en cada entrada (`configSource`).

Resolución: coincidencia exacta por `Build.SOC_MODEL` → codename
(`Build.HARDWARE`/`BOARD`) → familia por prefijo del modelo → genérico
ARM64 conservador (búfer 512, 2 hilos). Un SoC desconocido nunca es un
error: es un perfil genérico.

## Optimizaciones (registro cerrado) y su rollback

| # | Optimización | Actuador hoy | Rollback |
|---|---|---|---|
| 1 | RESOURCE_BUDGET — presupuesto al ThermalGovernor (0/1/2) | **Real** (`ThermalGovernor.setExternalResourceBudget`) | Vuelve a NORMAL (0). |
| 2 | ROUTE_PROFILE — refresco del perfil de ruta | **Real** (`AudioRouteManager.refreshResourceBudgetProfile`) | No-op (el refresco no deja estado). |
| 3 | BUFFER_SIZE — bloque de IVANNA al recomendado | No disponible: sin setter de bloque en la ruta de audio todavía. | N/A (no se aplica). |
| 4 | THREAD_AFFINITY — hilos DSP a núcleos grandes | No disponible: falta el puente JNI. La implementación C++ existe (`ivanna_set_thread_affinity` en `device_cpu_profiler.hpp`). | N/A. |
| 5 | SAMPLE_RATE — SR de trabajo a la nativa del HAL | No disponible: la SR se fija en `nativeInitDSP` (init, no en caliente). | N/A. |
| 6 | NEON_PATH — ruta vectorizada NEON | No disponible: se elige en compilación por SoC. | N/A. |
| 7 | DSP_OFFLOAD — offload al DSP del SoC | No disponible: sin seam en el HAL propietario. | N/A. |

Verificación post-aplicación (cuando hay métricas): si la latencia sube
> 5 ms o los underruns suben tras aplicar, el paso se revierte
automáticamente al valor anterior (`REVERTED`). `rollbackAll()` revierte
en orden inverso todas las aplicadas.

## Límites honestos

- **El software no cambia el hardware físico.** No sube frecuencias de CPU
  (el Power HAL del OEM las gobierna y revierte los writes), no cambia el
  DAC, los altavoces ni la acústica de la sala. Lo que optimiza es CÓMO se
  usa el hardware: afinidad, búferes, rutas NEON, sample rates, presupuesto
  de recursos.
- **"Sin margen de error" = fail-closed con rollback**, no magia: cada
  optimización guarda el valor anterior y se revierte ante regresión
  medida. Lo que no tiene actuador se reporta como UNAVAILABLE con el
  motivo exacto; nunca se finge éxito.
- **Dispositivos desconocidos** usan el perfil genérico conservador + la
  medición en vivo. No se inventan datos de SoC.
- **La latencia medida** (`measuredOutputLatencyMs`) solo existe si
  `RoomMeasurement` la midió en vivo; el perfilador no la estima.
- **El DSP del SoC** (Hexagon/APU/NPU/TPU) se detecta pero el offload no
  está disponible hoy: el HAL de audio es propietario y no hay seam
  (ver misión HAL–ASRC). La oportunidad queda registrada, no aplicada.
- **Seguridad**: el registro de optimizaciones es cerrado; no existe
  ninguna capacidad de escribir en sysfs, tocar SELinux, frecuencias,
  SafetyLimiter, IntegrityGate ni pins. Esas rutas no tienen actuador por
  diseño.
- **Root/Magisk** solo se DETECTAN (lectura de archivos); el sistema nunca
  ejecuta `su` ni escribe en `/data/adb`.

## Tests

- **Nativo** (`test_device_cpu_profiler.cpp`, standalone con main propio):
  27 checks, 0 fallos en host. Compilación:
  `g++ -std=c++17 -Wall -Wextra -I app/src/main/cpp app/src/main/cpp/tests/test_device_cpu_profiler.cpp -o /tmp/test_device_cpu_profiler -pthread`
  Registro en CI (1 línea, para el integrador — este trabajo no toca
  `tests/CMakeLists.txt`): `ivanna_add_test(test_device_cpu_profiler)`.
- **Kotlin JVM** (`app/src/test/.../device/`, 37 tests: parser 9, base 8,
  auditor 9, optimizador 11): lógica pura sin framework Android. **Sin
  kotlinc en la VM: la CI debe compilarlos y correrlos.**
- **Suite host C++ existente**: 848/848 en verde tras este trabajo
  (sin regresiones; los archivos nuevos no están registrados en CMake y no
  afectan a la suite).
