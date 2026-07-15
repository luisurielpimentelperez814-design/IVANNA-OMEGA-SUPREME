# IVANNA_ARCHITECTURE_DECISION_REPORT.md

Prioridad 1.5 — clasificación arquitectónica y plan de rehabilitación.
Basado en `IVANNA_PLACEBO_AUDIT_REPORT.md`. Ningún cambio de código en producción en este
reporte — solo análisis y clasificación.

Clasificación usada: **A) CONSERVAR · B) REHABILITAR · C) ARCHIVAR · D) ELIMINAR FUTURO**

---

## AudioEngine / audio_orchestrator.cpp

**Estado actual:** huérfano confirmado (Prioridad 1) — se inicializa y configura al arrancar la
app, pero `nativeProcessAudio()` nunca recibe un buffer real.

**Clasificación: B) REHABILITAR — parcial, no completo**

**Evidencia:**
- Contiene `EQState`/`BiquadCoefs` (EQ propio, redundante con `ParametricEQ` de `DSPBridge` — sí
  duplica).
- Contiene `KalmanState`/`Hyperplane` (posible prototipo anterior de lo que hoy son
  `phase_oracle.cpp`/`shm_hyperplane.cpp`, ya vivos y wireados por separado — probablemente
  redundante también, pendiente confirmar).
- **Contiene `process_limiter()` — un limiter brickwall real a -0.1dBFS (`LIMITER_THRESH =
  0.98855f`, `LIMITER_CEIL = 0.989f`), aplicado al final de la cadena.**
- Verificado: la cadena viva (`nativeProcess()` en `ivanna_omega_jni.cpp`, confirmada
  end-to-end en la Fase C de esta sesión) **no tiene NINGÚN limiter ni clamp de amplitud final**
  — el único `clamp` que existe en toda la función es para un coeficiente de correlación
  espacial, no para la señal de salida.

**¿Duplica la ruta DSP actual?** Sí, en EQ y probablemente en Kalman/Hyperplane — no aporta ahí.

**¿Tiene algoritmos superiores?** No en EQ. **Sí en el limiter — es una pieza que falta en la
ruta real y que aquí ya existe, probada y compilando.**

**¿Puede convertirse en Adaptive DSP Engine futuro?** Ambicioso para este momento; no lo
recomiendo como objetivo inmediato. El valor extraíble concreto es más acotado.

**Valor estratégico:** ALTO, pero no por su arquitectura de orquestador — por el limiter que
tiene y que la cadena real necesita.

**Riesgo:** con `MASTER GAIN` llegando a +18dB (cableado en esta misma sesión) y el `drive` del
exciter permitiendo saturación fuerte, la ruta real de audio puede clipear digitalmente sin
ningún techo de protección. Esto es un riesgo de calidad/reputación real, no solo prolijidad de
código.

**Acción recomendada:** NO migrar el orquestador completo. Extraer únicamente
`process_limiter()` (y sus dos constantes) hacia la cadena real de `DSPBridge` (después de
`g_gain.processOutput()`, antes de re-intercalar al buffer de salida). El resto del archivo
(EQState/Kalman/Hyperplane) queda en **C) ARCHIVAR** salvo que se confirme que
`phase_oracle.cpp`/`shm_hyperplane.cpp` NO ya cubren esa función (pendiente de verificar).

**Prioridad: ALTA** (es una brecha de seguridad de audio real, no solo limpieza).

---

## SystemAudioCapture

**Estado actual:** huérfano pero funcional — nunca se instancia.

**Clasificación: B) REHABILITAR**

**Evidencia:**
- Usa `AudioPlaybackCaptureConfiguration` + `MediaProjection` (la misma API segura que ya usa
  `PlaybackCaptureService`) — **no** usa el micrófono físico, así que no reintroduce el patrón de
  eco que ya se corrigió en `SpatialAudioEngineV2` esta sesión.
- Tiene comentarios de fixes propios (leak de `MediaProjection`, liberación explícita) que
  sugieren una iteración más reciente/refinada que `PlaybackCaptureService`.

**¿Puede convertirse en captura universal?** Sí, ya usa la API correcta para eso.
**¿Compatible con AudioPlaybackCapture API?** Sí, es literalmente lo que usa.
**¿Problemas de permisos Android?** Ninguno adicional a los que ya maneja
`PlaybackCaptureService` (mismo modelo de consentimiento MediaProjection).
**¿Puede alimentar IvannaLab?** Sí, en principio — mismo tipo de buffer que ya consumen
`IvannaNpeEngine`/`SpatialAudioEngineV2`.

**Valor estratégico:** MEDIO-ALTO. Si es genuinamente una versión mejorada de
`PlaybackCaptureService`, vale la pena evaluar reemplazo, no solo "activar otro capturador en
paralelo" (eso sí sería duplicación real).

**Riesgo:** si se activa SIN comparar contra `PlaybackCaptureService`, se puede terminar con DOS
capturas de `MediaProjection` compitiendo — Android típicamente no permite múltiples sesiones de
`AudioPlaybackCaptureConfiguration` simultáneas del mismo tipo de forma limpia.

**Acción recomendada:** comparar línea por línea contra `PlaybackCaptureService` antes de decidir
si reemplaza o se archiva. No activar ambas a la vez bajo ninguna circunstancia.

**Prioridad: MEDIA** — no es urgente, pero tiene valor real si resulta ser la versión superior.

---

## UsbAudioProManager

**Estado actual:** huérfano, ya documentado honestamente en el README como "en progreso".

**Clasificación: B) REHABILITAR — no archivar**

**Evidencia:** implementa acceso USB OTG de bajo nivel a DACs de audio, bypaseando el mezclador
nativo de Android (transferencia bulk/isochronous directa al endpoint de audio) — 213 líneas de
lógica real de protocolo USB Audio Class, no un stub.

**Estado actual USB DAC:** bloqueado en el dump real del descriptor del DAC (confirmado por
README y por el propio historial de commits de esta sesión paralela).
**Compatibilidad Android USB Audio:** la clase ataca el problema al nivel correcto (endpoints USB
directos), que es el único camino real para bypasear la limitación de Android de exponer DACs
USB como dispositivo de salida genérico sin control fino.
**Valor estratégico para "AUX Reference Supreme":** ALTO — es exactamente la ruta Hi-Fi de mayor
fidelidad posible en Android (evita el resampling/mezclador del framework), coincide con el
posicionamiento de audiófilo del proyecto.

**No se archiva** — el propio protocolo de este documento lo excluye explícitamente ("No
archivar si representa la ruta Hi-Fi principal"), y la evidencia confirma que sí lo es.

**Riesgo:** ninguno nuevo — sigue bloqueado en lo mismo que ya se sabía.

**Acción recomendada:** mantener como REHABILITAR pendiente, sin tocar hasta resolver el
descriptor del DAC. No es trabajo de esta fase.

**Prioridad: BAJA por ahora** (bloqueada externamente, no por decisión de arquitectura).

---

## AudioRoutingManager vs. AudioRouteManager

| Clase | Referencias reales | Función real | Mantener |
|---|---|---|---|
| `AudioRouteManager` (class, 132 líneas) | 0 (huérfana) | Detecta ruta de salida activa (BT/AUX/USB/altavoz) y aplica perfil de compensación completo vía `control_set_route_profile()` en `audio_control_plane.hpp` — **destino ya cableado y confirmado VIVO** en la cadena real | **Sí — B) REHABILITAR** |
| `AudioRoutingManager` (object, 36 líneas) | 0 (huérfana) | Utilidad de una sola función: fuerza el routing de salida a un DAC USB específico vía `AudioTrack.preferredDevice` | Sí — B) REHABILITAR, pero como utilidad separada, no fusionada |

**No son duplicados funcionales** — nombres casi idénticos, propósitos distintos (compensación
tonal por ruta vs. selección forzada de dispositivo USB).

**Arquitectura única propuesta:** conservar ambas clases, pero renombrar para eliminar la
confusión del nombre:
- `AudioRouteManager` → `AudioRouteCompensationManager` (más descriptivo de lo que realmente
  hace)
- `AudioRoutingManager` → `UsbDacForcedRouting` (idem)

Esto es un rename, no una fusión — fusionar las dos metería la selección forzada de dispositivo
USB (una acción puntual, imperativa) dentro de una clase que hoy es reactiva
(`AudioDeviceCallback`, estado continuo) — mezclaría dos modelos de ciclo de vida distintos sin
necesidad real.

**Prioridad: MEDIA** — `AudioRouteManager` es el huérfano de mayor prioridad de rehabilitación de
todo el informe anterior (compensación real para Bluetooth lossy y AUX, con destino ya listo del
lado nativo); `AudioRoutingManager` puede esperar a que se resuelva USB DAC en general.

---

## OmegaApplication vs. IVANNAApplication

**Cuál es oficial:** `IVANNAApplication` — única declarada en
`AndroidManifest.xml:31` (`android:name=".core.IVANNAApplication"`). Confirmado, no admite
ambigüedad: Android solo permite una clase `Application` por APK.

**¿OmegaApplication contiene lógica recuperable?** No. Su `onCreate()` completo es:
```kotlin
override fun onCreate() {
    super.onCreate()
    OmegaEngine.init(this)
    Log.i("IVANNA-OMEGA", "Application initialized — GORE TNS")
}
```
`IVANNAApplication.onCreate()` ya llama `OmegaEngine.init(this)` (confirmado en la auditoría de
esta sesión) — no hay ninguna línea en `OmegaApplication` que no exista ya, y mejor implementada,
en `IVANNAApplication`.

**¿Debe fusionarse o eliminarse?** No hay nada que fusionar — es estrictamente un subconjunto
inalcanzable de lo que ya hace la clase oficial.

**Clasificación: C) ARCHIVAR** (no D — no es peligroso ni confuso para el compilador, solo
inalcanzable; pero tampoco tiene ningún valor recuperable que justifique conservarlo activo).

**Prioridad: BAJA** — cero riesgo actual, es limpieza pura sin urgencia.

---

## THD falso

**No se toca el código todavía**, según regla del protocolo. Plan de separación:

### THD estimado perceptual (nombre nuevo, UI actual)
- Sigue siendo la fórmula actual (`thd_pred` sobre clarity/warmth) — es barata, ya calculada, y
  tiene valor como *proxy* de "qué tan sucio suena el material" para decisiones de Auto-IA
  internas.
- **Renombrar en la UI** de "THD" a algo que no reclame ser una medición física: propuesta —
  "Distorsión percibida (est.)" o "Aspereza tímbrica" — cualquiera que dijere claramente que es
  una estimación derivada de clasificación, no una medición.
- Puede seguir alimentando decisiones internas del `AutonomousBrain`/Auto-IA sin cambios, ya que
  ahí nunca se le llamó "THD" a los ojos del usuario — es solo la etiqueta de la UI la que
  miente.

### THD medido real (futuro, IvannaLab)
- Requiere FFT real sobre el buffer de audio: detectar fundamental, medir energía en 2f/3f/4f/...
  relativa a la fundamental, calcular %THD+N si se quiere ser riguroso (incluyendo ruido).
- Candidato natural para vivir en un módulo de diagnóstico separado ("IvannaLab", mencionado en
  el documento) en vez de correr en cada bloque de audio en producción — el análisis FFT tiene
  costo de CPU que no se justifica correr siempre, solo cuando el usuario pide un diagnóstico
  explícito.
- No implementar en esta fase — es trabajo nuevo, no rehabilitación de código existente.

**Regla aplicada:** nunca más usar una heurística con nombre de medición física — este mismo
principio debería aplicarse retroactivamente a revisar si hay otras métricas con el mismo
problema (no se auditó eso en esta pasada; candidato para una futura Prioridad 1.6 si se
quiere).

**Prioridad: MEDIA** (el rename es barato y cierra un riesgo de credibilidad; la medición real es
trabajo nuevo, no urgente).

---

## Resumen de clasificación final

| Componente | Clasificación | Prioridad |
|---|---|---|
| `process_limiter()` de audio_orchestrator.cpp | B) REHABILITAR (extracción quirúrgica) | **ALTA — riesgo de seguridad de audio real** |
| Resto de audio_orchestrator.cpp (EQ/Kalman/Hyperplane) | C) ARCHIVAR (pendiente confirmar solape con phase_oracle/shm_hyperplane) | Baja |
| SystemAudioCapture | B) REHABILITAR (tras comparar con PlaybackCaptureService) | Media |
| UsbAudioProManager | B) REHABILITAR (bloqueada externamente) | Baja por ahora |
| AudioRouteManager | B) REHABILITAR + rename | Media-Alta |
| AudioRoutingManager | B) REHABILITAR + rename | Media |
| OmegaApplication.kt | C) ARCHIVAR | Baja |
| "THD" en UI | Plan de separación (rename inmediato + medición real futura) | Media |

**Ningún D) ELIMINAR FUTURO en este informe** — no se encontró código genuinamente peligroso o
confuso que amerite esa categoría; todo lo huérfano tiene valor recuperable real o es limpieza de
bajo riesgo (C).

**Hallazgo que cambia la prioridad global:** el limiter faltante en la cadena real es más
urgente que cualquier limpieza — es el único punto de este informe con riesgo directo sobre lo
que el usuario escucha, no solo sobre la salud del repositorio.
