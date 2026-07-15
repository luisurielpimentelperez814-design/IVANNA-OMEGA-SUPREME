# IVANNA_PLACEBO_AUDIT_REPORT.md

Auditoría de código placebo y falsos positivos — Prioridad 1.
Commit base: `02c3889` (fix orden de gain staging, Input Trim antes de EQ/Comp/Exciter/Widener).
Metodología: verificación directa contra código ejecutable (grep de instanciación/llamada real,
lectura de la cadena constructor → setParams → coeficientes → process → buffer), no contra
documentación ni nombres de archivo.

---

## 1. Controles UI desconectados

### Exciter / EQ / Stereo Width / Compressor Threshold / Ratio

Ruta: `app/src/main/java/com/ivanna/omega/MainActivity.kt:428-522`
Línea: 428, 443, 456, 495, 516
Estado: ✅ VIVO
Categoría: N/A
Evidencia: cada callback llama `dspState = dspState.copy(...)` seguido de `dspState.pushToNative()`.
Flujo esperado: UI → DSPState → DSPBridge.setParams()/setStereoWidth() → g_eq/g_comp/g_exciter/g_widener/g_gain → buffer real.
Flujo real: confirmado idéntico al esperado. `pushToNative()` además espeja a `globalEffectManager`
(AudioEffect de terceros) y `omegaBridge` (daemon Magisk) — triple camino simultáneo.
Impacto: ninguno, funciona como se espera.
Recomendación: ninguna.

### NHO Harmonic / Spatial Angle / Spatial Width

Ruta: `MainActivity.kt:523-536`
Estado: ✅ VIVO
Evidencia: `IvannaNativeLib.nativeSetHarmonicGain/nativeSetSpatialAngleRad/nativeSetSpatialWidth`
modifican el estado interno de `g_pd` (PDEngine). Se verificó en
`app/src/main/cpp/jni/ivanna_omega_jni.cpp:200-262` que `g_pd.process_block(chL, chR, pdOutL,
pdOutR, n)` se llama dentro de `nativeProcess()` (la única función que el audio real invoca por
bloque) y que `pdOutL/pdOutR` terminan escritos en `data[2*i]/data[2*i+1]` — el buffer que sale
por `AudioTrack`.
Flujo real: UI → IvannaNativeLib → g_pd (PDEngine) → process_block() → buffer final. Confirmado
end-to-end.
Impacto: ninguno.
Recomendación: ninguna.

### Anti-Dolby toggle / Presets

Ruta: `MainActivity.kt:466-483`
Estado: ✅ VIVO
Evidencia: ambos llaman `globalEffectManager.applyProfile(...)`, que modifica sesiones
`AudioEffect` activas de apps de terceros (Equalizer/BassBoost/Virtualizer/DynamicsProcessing).
Impacto: ninguno.
Recomendación: ninguna.

### OmegaMetrics

Ruta: `app/src/main/java/com/ivanna/omega/audio/OmegaMetrics.kt`
Línea: 1-6
Estado: 💀 HUÉRFANO
Categoría: variable/estructura que existe pero nunca se instancia
Evidencia: `data class OmegaMetrics(rmsLevel, peakLevel, clipCount)` — `grep -rn
"OmegaMetrics("` en todo el repo solo encuentra la propia declaración. Ningún archivo construye
una instancia ni la lee.
Flujo esperado: audio real → cálculo RMS/Peak/clips → OmegaMetrics → UI.
Flujo real: no existe ningún eslabón — la estructura está definida y nunca se usa.
Impacto: ninguno sobre el audio (no se llama), pero es una API de telemetría que parece existir
y no aporta nada.
Recomendación: conservar si hay intención de usarla pronto; si no, marcar explícitamente como
no usada en un comentario para que no se asuma funcional.

---

## 2. Mediciones declaradas pero no calculadas

### "THD" en el panel de control (StatBlock)

Ruta: `app/src/main/java/com/ivanna/omega/ui/IvannaControlPanel.kt`
Línea: 369
Estado: ❌ Placeholder (heurística disfrazada de medición real)
Categoría: mediciones declaradas pero no calculadas
Evidencia: el valor viene de `npeClassifyThd`, calculado en
`app/src/main/cpp/neuromorphic/ivanna_synthesizer.hpp:96-99`:
```cpp
// THD estimada: material poco claro (clarity baja) y cálido (warmth alto)
// produce más distorsión armónica percibida. Rango 0.1%-3%.
const float thd_pred = 0.5f + 1.5f * ((1.f - current_[4]) * 0.5f) *
                               ((1.f + current_[3]) * 0.5f);
```
`current_[4]` y `current_[3]` son los features "clarity" y "warmth" del clasificador de género/
timbre — NO hay ningún análisis espectral/FFT de armónicos sobre la señal real. THD de verdad
requiere medir energía en 2f, 3f, 4f... relativa a la fundamental; esto es una fórmula lineal
sobre dos scores de clasificación completamente ajenos.
Flujo esperado: buffer de audio → FFT → detección de fundamental → energía en armónicos →
THD real (%).
Flujo real: clasificador de género (clarity, warmth) → fórmula lineal → valor con la etiqueta
"THD".
Impacto: el usuario ve un número con nombre de métrica de ingeniería de audio real que no mide
lo que dice medir. Riesgo de credibilidad si algún reviewer técnico lo verifica.
Recomendación: renombrar en la UI a algo honesto ("Distorsión percibida (estimada)" o similar) o
implementar THD real vía FFT si se quiere que el nombre sea literal.

### RMS / gain_db del AGC del daemon (GET_TELEMETRY)

Ruta: `app/src/main/cpp/omega_effect.cpp:130,146`
Estado: ✅ Cálculo real con datos de audio
Evidencia: `ctx->shared->ai_rms_level.store(ctx->agc_envelope, ...)` y
`ctx->shared->ai_gain_db.store(gain_db, ...)` — ambos derivados de un envelope follower que
procesa el buffer de audio real dentro de `Effect_Process()`, el callback que AudioFlinger invoca
por bloque en el HAL. Confirmado real, no placeholder.
Impacto: ninguno.
Recomendación: ninguna.

---

## 3. Cadenas DSP sin efecto

### AudioEngine / audio_orchestrator.cpp

Ruta: `app/src/main/java/com/ivanna/omega/audio/AudioEngine.kt`,
`app/src/main/cpp/audio_orchestrator.cpp`
Línea: `MainActivity.kt:749-752,805`
Estado: 💀 HUÉRFANO
Categoría: objeto DSP instanciado sin process() con audio real
Evidencia: `audioEngine.initialize(48000)` + `setExciter/setEqGain/setWidth` se llaman UNA VEZ al
arrancar la app (valores iniciales de `parameterStore`), y `audioEngine.release()` al cerrar. En
ningún lugar del código se llama `nativeProcessAudio()` con un buffer real — no hay
`AudioRecord`/loop de captura conectado a esta clase.
Flujo esperado: Constructor → setParams → coeficientes nativos → process(buffer real) → salida.
Flujo real: Constructor → setParams → coeficientes nativos → (nada — process() nunca se invoca
con audio).
Impacto: JNI calls reales al arrancar/cerrar la app, cero efecto audible. Es el ejemplo más claro
de "efecto que suma latencia sin modificar señal" del checklist original.
Recomendación: eliminar las 5 llamadas de `MainActivity.kt:749-752,805` (ya no son necesarias —
Exciter/EQ/Width en vivo van por `DSPState.pushToNative()` desde Fase A), o si se planea usar
`audio_orchestrator.cpp` para algo específico a futuro, documentar esa intención y dejar de
llamarlo mientras tanto.

### SystemAudioCapture

Ruta: `app/src/main/java/com/ivanna/omega/audio/SystemAudioCapture.kt`
Estado: 💀 HUÉRFANO (funcional, pero apagado)
Categoría: clase real y completa, nunca instanciada
Evidencia: `startCapture()` abre un `AudioRecord` real y tiene loop de lectura funcional (línea
139-205), pero `grep -rln "SystemAudioCapture("` no encuentra ninguna instanciación fuera de su
propio archivo.
Impacto: ninguno sobre el audio actual (simplemente no corre). Distinto de los demás huérfanos:
esta SÍ funcionaría si se conectara.
Recomendación: si es para una feature futura de captura sin MediaProjection, documentarlo
explícitamente; si es obsoleta, marcarla.

### UsbAudioProManager

Ruta: `app/src/main/java/com/ivanna/omega/audio/UsbAudioProManager.kt`
Estado: 💀 HUÉRFANO
Evidencia: nunca instanciado. Consistente con el README, que ya documenta "Soporte USB DAC
dedicado — en progreso, bloqueado en el dump real del descriptor del DAC".
Impacto: ninguno (ya documentado como pendiente).
Recomendación: ninguna adicional — el README ya es honesto sobre esto.

### AudioRouteManager

Ruta: `app/src/main/java/com/ivanna/omega/audio/AudioRouteManager.kt`
Estado: 💀 HUÉRFANO (con destino real ya cableado del lado nativo)
Categoría: variable que cambiaría el DSP pero nunca se dispara desde la UI/lifecycle
Evidencia: el propio docstring de la clase referencia
`AudioEngine.nativeSetRouteProfile()` → `control_set_route_profile()` en
`audio_control_plane.hpp` (que SÍ es parte de la cadena real, confirmada en la Fase B de esta
sesión). Pero `grep -rn "AudioRouteManager("` no encuentra ninguna instanciación en todo el
repo — la detección de ruta de salida (Bluetooth/AUX/USB/altavoz) nunca se dispara.
Impacto: la compensación por ruta de salida (crítica para códecs Bluetooth lossy y rolloff de
graves en AUX, según su propio docstring) simplemente no ocurre nunca, aunque el punto de
enganche en C++ esté listo y probablemente funcional.
Recomendación: de los huérfanos encontrados, este es el de mayor prioridad para rehabilitar —
el trabajo pesado (control_set_route_profile en el control plane real) ya existe; solo falta
instanciar la clase y registrar el `AudioDeviceCallback` desde `MainActivity`/`IVANNAApplication`.

### AudioRoutingManager

Ruta: `app/src/main/java/com/ivanna/omega/audio/AudioRoutingManager.kt`
Estado: 💀 HUÉRFANO
Evidencia: `object` con un único método `forceUsbDacRouting()`. `grep -rn
"forceUsbDacRouting"` no encuentra llamadas fuera de su propia declaración.
Impacto: ninguno (no se ejecuta).
Recomendación: ver sección 4 — no es un duplicado funcional de `AudioRouteManager`, pero ambos
están muertos por separado.

---

## 4. Duplicados arquitectónicos

### AudioRouteManager vs. AudioRoutingManager

Ruta: `app/src/main/java/com/ivanna/omega/audio/AudioRouteManager.kt` (132 líneas, class) y
`AudioRoutingManager.kt` (36 líneas, object)
Estado: ❌ NO son duplicados funcionales — nombres casi idénticos, propósitos distintos
Evidencia:
- `AudioRouteManager` (clase): detecta la ruta de salida activa (BT/AUX/USB/altavoz) y aplica
  un perfil de compensación completo vía el control plane real.
- `AudioRoutingManager` (object): utilidad de una sola función, fuerza el routing de salida a un
  DAC USB específico vía `AudioTrack.preferredDevice` — no tiene relación con perfiles de
  compensación DSP.
Ambos están huérfanos (sección 3), pero por separado — no compiten por el mismo rol.
Recomendación: **conservar ambos** (no eliminar), pero renombrar uno para evitar la confusión
del nombre casi idéntico (p. ej. `AudioRouteManager` → `AudioRouteCompensationManager`, o
`AudioRoutingManager` → `UsbDacForcedRouting`) antes de rehabilitarlos, para que quede claro en
el propio nombre cuál hace qué.

### IVANNAApplication vs. OmegaApplication

Ruta: `app/src/main/java/com/ivanna/omega/core/IVANNAApplication.kt` (registrada) vs.
`OmegaApplication.kt` (no registrada)
Estado: 💀 CÓDIGO MUERTO confirmado (ver auditoría previa de esta sesión)
Evidencia: `AndroidManifest.xml:31` declara `android:name=".core.IVANNAApplication"` —
`OmegaApplication` no aparece en ningún lado del manifest. Android nunca instancia esta segunda
clase.
Recomendación: eliminar `OmegaApplication.kt` (no aporta nada, es inalcanzable por diseño de
Android — un solo `<application android:name=...>` por APK) o, si se prefiere no borrar nunca,
añadir un comentario al inicio del archivo dejando explícito que está desconectada del manifest
a propósito/por error, para que nadie asuma que corre.

### Engines duplicados (OmegaEngine, DSPBridge, IvannaNativeLib, AudioEngine)

Estado: ❌ NO VERIFICADO exhaustivamente — hay 4 "puntos de entrada" nativos con nombres
parecidos y roles que se solapan parcialmente:
- `DSPBridge` — ✅ VIVO, cadena real (EQ/Comp/Exciter/Widener/Gain), confirmada extensamente.
- `IvannaNativeLib` — ✅ VIVO, controla `g_pd` (PDEngine: NHO/Spatial/HRTF/Evolutivo), confirmado
  en sección 1.
- `OmegaEngine` (object) — ✅ VIVO, `setMode()` confirmado cableado a la UI.
- `AudioEngine` (audio_orchestrator.cpp) — 💀 HUÉRFANO, confirmado en sección 3.
No se auditó en esta pasada si `DSPBridge` e `IvannaNativeLib` comparten o no el mismo `g_eq`/
`g_comp`/`g_widener`/`g_gain` por debajo (evidencia parcial de sesiones anteriores sugiere que sí,
son el mismo objeto estático compartido entre ambas clases JNI) — de ser así no son duplicados
sino dos fachadas Kotlin distintas sobre el mismo motor C++, lo cual es razonable y no amerita
limpieza. Recomendación: verificar explícitamente en una pasada futura antes de decidir cualquier
cosa aquí.

---

## Resumen ejecutivo

| Componente | Estado | Acción recomendada |
|---|---|---|
| Exciter/EQ/Width/Comp sliders | ✅ VIVO | ninguna |
| NHO/Spatial sliders | ✅ VIVO | ninguna |
| Anti-Dolby/Presets | ✅ VIVO | ninguna |
| RMS/gain_db del daemon | ✅ VIVO | ninguna |
| OmegaMetrics | 💀 HUÉRFANO | conservar o marcar explícitamente sin uso |
| "THD" en el panel | ❌ Placeholder | renombrar honesto o implementar FFT real |
| AudioEngine/audio_orchestrator.cpp | 💀 HUÉRFANO | quitar las 5 llamadas en MainActivity |
| SystemAudioCapture | 💀 HUÉRFANO (funcional) | documentar intención o marcar obsoleta |
| UsbAudioProManager | 💀 HUÉRFANO | ya documentado en README, sin acción extra |
| AudioRouteManager | 💀 HUÉRFANO (prioridad de rehabilitación) | instanciar + registrar callback |
| AudioRoutingManager | 💀 HUÉRFANO | conservar, renombrar para evitar confusión |
| OmegaApplication.kt | 💀 CÓDIGO MUERTO | eliminar o comentar explícitamente |
| Engines (Omega/DSPBridge/NativeLib/AudioEngine) | parcialmente ❌ NO VERIFICADO | verificar solape de g_eq/g_comp antes de decidir |

No se corrigió nada en esta pasada — solo mapeo, como pide el protocolo. Prioridad 1.5
(decidir qué eliminar/rehabilitar/conservar) queda pendiente de tu decisión.
