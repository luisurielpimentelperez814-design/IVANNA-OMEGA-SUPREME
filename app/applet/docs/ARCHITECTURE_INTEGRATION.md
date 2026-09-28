# IVANNA-OMEGA-SUPREME — Plan Maestro de Integración v2.0

**Objetivo:** Conectar los motores desconectados (YAMNet, AudioEngine, Spatial, Evolutionary, Phase Oracle) al pipeline principal **sin borrar, degradar ni perder capacidad**.

---

## 🎯 Motores Huérfanos Identificados

| Motor | Estado | Problema | Impacto |
|-------|--------|----------|--------|
| **YAMNet Classifier** | Corre pero aislado | Clasifica pero no actualiza PDEngine | Scores de IA nunca llegan al hot-path |
| **AudioEngine** | Independiente | Parámetros sin sincronización | Exciter/EQ/Width no se fusionan |
| **Spatial Engine (ITD/ILD)** | Existe en C++ | No se mezcla en salida real | Renderizado sin efectos |
| **Evolutionary Kernel** | Offline desacoplado | Genomas optimizados no se aplican en tiempo real | GA solo experimental |
| **Phase Oracle** | No integrado | Predice pero no se usa | BiquadBank sin predicción de fase |
| **OmegaEngineBridge** | Socket frágil | Sin reconexión automática | Daemon Magisk: desconexiones silenciosas |

---

## 🏗️ Arquitectura Nueva: Control Plane Unificado

```
┌─────────────────────────────────────────────────────────────────┐
│                     AudioForegroundService                      │
│                        (Audio thread)                            │
└─────────────────────────────────────────────────────────────────┘
                               ↓
┌─────────────────────────────────────────────────────────────────┐
│                   UnifiedAudioControlPlane                       │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  1. AudioPipeline.process() captura buffer              │   │
│  │     ↓                                                    │   │
│  │  2. YAMNetAdapter integrado                             │   │
│  │     → downsample 48k→16k                                │   │
│  │     → YamnetClassifier.classify() cada ~1s              │   │
│  │     → scores → nativeSetAntiDolbyScores() [NUEVA]       │   │
│  │     ↓                                                    │   │
│  │  3. DSPBridge.process(buffer)                           │   │
│  │     → g_eq, g_comp, g_exciter, g_widener               │   │
│  │     ↓                                                    │   │
│  │  4. PDEngine.process(dryL, dryR)                        │   │
│  │     → PhaseOracle.predict() → T_refined para biquads    │   │
│  │     → NHOEngine.process()                               │   │
│  │     → EvolutionaryAdapter mapea best_genome → params    │   │
│  │     → BiquadEnvelopeBank (con predicción de fase)       │   │
│  │     → CueBasedSpatial (ITD/ILD renderizado)             │   │
│  │     ↓                                                    │   │
│  │  5. AudioEngine fusión (AudioEngine parámetros)         │   │
│  │     → Mezcla: 60% dry + 20% spatial + 20% state        │   │
│  │     ↓                                                    │   │
│  │  6. Output limiter -0.1 dBFS                            │   │
│  │     ↓                                                    │   │
│  │  7. AudioTrack.write()                                  │   │
│  └──────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                               ↑
                (control/monitoring vía JNI)
                               ↑
┌─────────────────────────────────────────────────────────────────┐
│                        MainActivity                              │
│  ├─► Toggle Anti-Dolby → IvannaGlobalEffectManager.applyProfile │
│  ├─► Sliders → DSPState.pushToNative()                          │
│  ├─► NHO controls → IvannaNativeLib (PDEngine setters)          │
│  ├─► Evo kernel → nativeStartEvoThread / nativeStopEvoThread    │
│  └─► Telemetry → OmegaEngineBridge (con reconexión automática)  │
└─────────────────────────────────────────────────────────────────┘
```

---

## 📋 Plan de Implementación (7 Fases)

### **Fase 1: Orquestador Central (audio_control_plane.hpp/cpp)**
- [ ] Crear estructura `UnifiedControlFrame` que almacena estado de todos los motores
- [ ] Implementar `applyControlFrame()` como punto único de sincronización
- [ ] Thread-safe: usar `std::atomic<>` para parámetros compartidos

### **Fase 2: YAMNet → PDEngine Integration**
- [ ] `AudioPipeline.kt`: acumular muestras → downsample → clasifica
- [ ] Llamada a `nativeSetAntiDolbyScores()` cada ~1s (desde Kotlin)
- [ ] `audio_orchestrator.cpp`: recibir scores e inyectarlos en `g_pd`

### **Fase 3: AudioEngine Fusión Paramétrica**
- [ ] `AudioEngine` expone parámetros vía `nativeGetExciterValue()`, etc.
- [ ] `PDEngine.applyControlFrame()` lee estos valores
- [ ] Mezcla: 60% DSP + 20% Spatial + 20% AudioEngine state

### **Fase 4: Phase Oracle → BiquadBank**
- [ ] `PhaseOracle` predice `T_refined` (refinement de fase)
- [ ] `BiquadEnvelopeBank` usa `T_refined` para ajustar coeficientes en tiempo real
- [ ] Predictor Kalman integrado al audio thread

### **Fase 5: Evolutionary Real-Time Mapping**
- [ ] `EvolutionaryAdapter` mapea `best_genome` → parámetros reales
- [ ] Escritura atómica: `genome[0..4]` → DSP, `genome[5..8]` → NHO, `genome[9..11]` → Spatial
- [ ] Frequency: cada 50ms (sin bloquear audio thread)

### **Fase 6: OmegaEngineBridge Hardening**
- [ ] Reconexión automática en `send()` si socket está cerrado
- [ ] `readResponse()` con timeout para evitar deadlock
- [ ] `isConnected` verifica `!isClosed`

### **Fase 7: Testing & Documentation**
- [ ] Casos de prueba: cada motor se conecta sin romper otros
- [ ] Benchmarking: latencia del orquestador
- [ ] Doc: diagrama de flujo actualizado

---

## 🔧 Orden de Commits

1. **docs: Plan maestro de integración v2.0** ← Tú estás aquí
2. **cpp: audio_control_plane.hpp/cpp** — orquestador central
3. **cpp: phase_oracle_engine.hpp refinements** — predicción de fase
4. **cpp: evolutionary_adapter.hpp refinements** — mapeo real-time
5. **cpp: pd_engine.hpp — applyControlFrame integration**
6. **kotlin: AudioPipeline.kt — YAMNet integration**
7. **kotlin: AudioEngine.kt — nativeGetExciterValue, etc.**
8. **kotlin: OmegaEngineBridge.kt — reconexión automática**
9. **cpp: audio_orchestrator.cpp — nativeSetAntiDolbyScores pleno**
10. **docs: ARCHITECTURE_INTEGRATION_COMPLETE.md** — final diagram

---

## ✅ Garantías de Preservación

- ❌ No se borran archivos existentes
- ❌ No se degradan capacidades (Magisk, no-root, rooted)
- ✅ Se crean nuevos módulos (audio_control_plane, evolutionary_adapter)
- ✅ Se actualizan integraciones (YAMNet → PDEngine, Phase Oracle → BiquadBank)
- ✅ Backward compatible: modo DSP-only aún funciona si deshabilitas PDEngine
- ✅ Thread-safe: audio thread no se bloquea por operaciones de sincronización

---

## 📊 Métricas Esperadas

| Métrica | Antes | Después |
|---------|-------|---------|
| Motores activos | 3 (DSP, AudioEngine, Magisk aislado) | 6 (todos integrados) |
| Latencia orquestador | N/A | <1ms (async mapping) |
| YAMNet → PDEngine | nunca | cada ~1s |
| Evolutionary genomas aplicados | offline | cada 50ms |
| Reconexiones socket automáticas | 0 | ilimitadas |

---

**Status:** En desarrollo (feature/unified-engine-integration)
**Responsable:** IVANNA-OMEGA-SUPREME team
**Fecha:** 2026-07-02
