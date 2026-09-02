# IVANNA-OMEGA-SUPREME — Fixes de Conectividad v1.5

## Resumen de Problemas Encontrados y Correcciones

---

### 🔴 FIX 1: `AudioSessionReceiver.kt` — ARCHIVO FALTANTE

**Problema:** `IvannaGlobalEffectManager` existe y es correcto, pero nunca recibía sesiones de audio de otras apps porque el `BroadcastReceiver` que escucha `OPEN_AUDIO_EFFECT_CONTROL_SESSION` no existía en el proyecto.  
**Efecto:** Los efectos globales (EQ, BassBoost, Virtualizer) nunca se aplicaban a Spotify/YouTube/etc.  
**Fix:** Creado `AudioSessionReceiver.kt` + registrado en `AndroidManifest.xml`.

---

### 🔴 FIX 2: `AndroidManifest.xml` — Application class incorrecta + componentes faltantes

**Problema 1:** `android:name=".core.OmegaApplication"` apuntaba a la clase vacía `OmegaApplication`, no a `IVANNAApplication` que contiene toda la inicialización real del DSP.  
**Problema 2:** `PlaybackCaptureService` declarado en código pero no en el Manifest → crash al intentar iniciarlo.  
**Problema 3:** `AudioSessionReceiver` no declarado (ver FIX 1).  
**Fix:** Corregida la clase Application + añadidos todos los componentes faltantes.

---

### 🔴 FIX 3: `IVANNAApplication.kt` — `globalEffectManager` inaccesible desde `AudioSessionReceiver`

**Problema:** `globalEffectManager` era privado al companion object. `AudioSessionReceiver` necesita accederlo via `context.applicationContext as IVANNAApplication`.  
**Fix:** Movido a propiedad de instancia pública. Añadido `OmegaEngine.init(this)` antes del scope IO.

---

### 🔴 FIX 4: `MainActivity.kt` — Toggle Anti-Dolby desconectado + sin permiso RECORD_AUDIO

**Problema 1:** El `Switch` de "Modo Anti-Dolby" tenía `onCheckedChange = { antiDolbyEnabled = it }` — solo actualizaba el estado visual local, nunca llamaba a `IvannaGlobalEffectManager.applyProfile()`.  
**Problema 2:** `AudioEngine.initialize()` se llamaba antes de solicitar el permiso `RECORD_AUDIO` → crash silencioso en Android 6+.  
**Problema 3:** `AudioForegroundService` nunca se iniciaba → el procesamiento moría cuando la app iba a background.  
**Fix:** Runtime permission + arranque del servicio + conexión real del toggle al `globalEffectManager`.

---

### 🟡 FIX 5: `AudioPipeline.kt` — YAMNet clasificaba pero sus resultados nunca llegaban al C++

**Problema:** El clasificador YAMNet (AI) corre en `SpectralClassifier` y en `YamnetClassifier`, pero ninguno llamaba a `nativeSetAntiDolbyScores()` en el orquestador C++. La clasificación AI existía pero era un "callejón sin salida" — los scores nunca ajustaban el widener/EQ del hot-path.  
**Fix:** `AudioPipeline` ahora acumula muestras, downsamplea 48kHz→16kHz, clasifica cada ~1s y llama a `AudioEngine.nativeSetAntiDolbyScoresStatic()`.

---

### 🟡 FIX 6: `AudioEngine.kt` + `ivanna_jni_stub.cpp` — JNI `nativeSetAntiDolbyScores` sin implementación en stub

**Problema:** `audio_orchestrator.cpp` implementa `Java_com_ivanna_omega_audio_AudioEngine_nativeSetAntiDolbyScores` (método de instancia), pero la ruta estática desde `AudioPipeline` → `AudioEngine.Companion` necesitaba `nativeSetAntiDolbyScoresJni` (`@JvmStatic`) que no existía en ningún `.cpp`.  
**Fix:** Añadida declaración `@JvmStatic` en companion, implementación en `ivanna_jni_stub.cpp`, y símbolo externo `ivanna_set_anti_dolby_scores()` en `audio_orchestrator.cpp`.

---

### 🟡 FIX 7: `AudioForegroundService.kt` — DSPBridge sin init tras reinicio START_STICKY

**Problema:** Con `START_STICKY`, si el sistema mata y reinicia el servicio, `onCreate()` se llama pero la Activity no. `DSPBridge` quedaba sin inicializar → silencio en el procesamiento de audio.  
**Fix:** `DSPBridge.init(48000)` ahora se llama en `onCreate()` del servicio.

---

### 🟡 FIX 8: `OmegaEngineBridge.kt` — Socket sin reconexión automática + respuestas no leídas

**Problema 1:** Si el daemon Magisk reiniciaba, el socket moría silenciosamente. Los siguientes `send()` tiraban `IOException` sin reconectar.  
**Problema 2:** `requestTelemetry()` enviaba `GET_TELEMETRY` pero nunca leía la respuesta → InputStream se llenaba y bloqueaba futuros envíos.  
**Problema 3:** `isConnected` no verificaba `isClosed` → falso positivo tras cierre del servidor.  
**Fix:** Reconexión automática en `send()`, método `readResponse()` con timeout, `isConnected` corregido.

---

## Diagrama de Flujo Corregido

```
MainActivity
   │ (permiso RECORD_AUDIO concedido)
   ├─► AudioEngine.initialize(48000)
   ├─► AudioForegroundService.start()
   │     └─► DSPBridge.init(48000)
   │         AudioPipeline.start()
   │           └─► DSPBridge.process(buf) ──────────────────┐
   │               feedYamnet(buf)                          │
   │                 └─► AudioEngine.nativeSetAntiDolbyScoresStatic()
   │                       └─► ivanna_set_anti_dolby_scores() (C++)
   │                             └─► gState.antiDolby.updateFromClassification()
   │                                   └─► widenerMultiplier / eqBoost2k4k
   │                                         └─► process_limiter() ◄──────────┘
   │
   └─► (toggle Anti-Dolby ON)
         └─► IVANNAApplication.globalEffectManager.applyProfile(SPATIAL)

IVANNAApplication.onCreate()
   ├─► OmegaEngine.init(context)  ← corrección: con Context real
   ├─► DSPBridge.init(48000)
   ├─► OmegaDaemon.start()
   └─► OmegaEngineBridge.connect()

AudioSessionReceiver.onReceive(OPEN_SESSION)  ← antes FALTABA
   └─► IVANNAApplication.globalEffectManager.openSession(sessionId)
         └─► Equalizer + BassBoost + Virtualizer + DynamicsProcessing
               aplicados a Spotify/YouTube/etc. ✅
```
