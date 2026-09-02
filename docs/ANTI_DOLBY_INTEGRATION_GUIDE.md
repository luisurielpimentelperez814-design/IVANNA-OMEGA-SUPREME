# Anti-Dolby: Guía de Integración con Captura de Audio

**Documento**: Cómo conectar audio real a YAMNet → AudioEngine  
**Status**: INMEDIATO (después de Fase 1)  
**Prioridad**: CRÍTICA para que Anti-Dolby funcione

---

## EL PROBLEMA: Audio Circula pero No Llega a YAMNet

Después de Fase 1, tienes:
- ✅ YAMNet instanciado
- ✅ AudioEngine conectado
- ✅ JNI `nativeSetAntiDolbyScoresStatic()` lista para ser llamada
- ❓ **PERO**: Nadie está pasando audio a `AntiDolbyController.processAudioFrame()`

**Sin captura de audio → YAMNet nunca clasifica → Scores siempre en cero → Anti-Dolby no hace nada.**

---

## PASO 1: Identificar Dónde Entra el Audio

En IVANNA-OMEGA-SUPREME, hay varias vías de captura:

### 1.1 PlaybackCaptureService (RECOMENDADO)

**Archivo**: `app/src/main/java/com/ivanna/omega/audio/PlaybackCaptureService.kt`

Este servicio captura **audio de reproducción** en tiempo real.

```kotlin
// Buscar en PlaybackCaptureService:
class PlaybackCaptureService : MediaProjectionManager {
    private var audioCallback: AudioRecord.OnAudioFrameAvailableListener? = null
    
    fun onAudioAvailable(audioData: FloatArray, sampleRate: Int, channelCount: Int) {
        // ← AQUÍ entra audio en tiempo real
    }
}
```

**Acción**: Agregar llamada a AntiDolbyController aquí.

### 1.2 SystemAudioCapture (ALTERNATIVA)

**Archivo**: `app/src/main/java/com/ivanna/omega/audio/SystemAudioCapture.kt`

Captura audio del sistema (aplicaciones, música, etc.).

```kotlin
class SystemAudioCapture : AudioRecordCallback {
    override fun onAudioFrame(buffer: ShortArray, sampleRate: Int) {
        // ← AQUÍ entra audio del sistema
    }
}
```

### 1.3 AudioCallbackManager (FALLBACK)

**Archivo**: `app/src/main/java/com/ivanna/omega/audio/AudioCallbackManager.kt`

Punto de agregación para callbacks de audio.

---

## PASO 2: Agregar Integración

### Opción A: PlaybackCaptureService (RECOMENDADA)

Abre `PlaybackCaptureService.kt` y busca el método que procesa audio:

```kotlin
// ANTES
private fun processAudioFrame(audioData: FloatArray, sampleRate: Int) {
    // Procesa efectos existentes
    effectManager.apply(audioData)
}

// DESPUÉS
private fun processAudioFrame(audioData: FloatArray, sampleRate: Int) {
    // 1. Resamplear a 16kHz si es necesario
    val audio16k = if (sampleRate != 16000) {
        AudioResampler.resample(audioData, sampleRate, 16000)
    } else {
        audioData
    }
    
    // 2. Convertir estéreo → mono si es necesario
    val audioMono = if (audio16k.size > 16000) {
        // Asumir estéreo: promediar canales
        FloatArray(audio16k.size / 2) { i ->
            (audio16k[i * 2] + audio16k[i * 2 + 1]) / 2f
        }
    } else {
        audio16k
    }
    
    // 3. Pasar a AntiDolbyController
    val app = context.applicationContext as IVANNAApplication
    app.antiDolbyController?.processAudioFrame(audioMono)
    
    // 4. Procesar efectos existentes
    effectManager.apply(audioData)
}
```

### Opción B: SystemAudioCapture

```kotlin
class SystemAudioCapture : AudioRecordCallback {
    override fun onAudioFrame(buffer: ShortArray, sampleRate: Int) {
        // Convertir ShortArray → FloatArray normalizado
        val floatBuffer = FloatArray(buffer.size) { i ->
            buffer[i].toFloat() / 32768f  // Normalizar a [-1.0, 1.0]
        }
        
        // Resamplear si es necesario
        val audio16k = if (sampleRate != 16000) {
            AudioResampler.resample(floatBuffer, sampleRate, 16000)
        } else {
            floatBuffer
        }
        
        // Pasar a AntiDolbyController
        val app = context.applicationContext as IVANNAApplication
        app.antiDolbyController?.processAudioFrame(audio16k)
    }
}
```

### Opción C: AudioCallbackManager

```kotlin
class AudioCallbackManager {
    fun onAudioData(buffer: FloatArray, sampleRate: Int, channelCount: Int) {
        // 1. Normalizar formato
        val audioMono = if (channelCount > 1) {
            // Estéreo → Mono
            FloatArray(buffer.size / channelCount) { i ->
                (0 until channelCount).map { ch ->
                    buffer[i * channelCount + ch]
                }.average().toFloat()
            }
        } else {
            buffer
        }
        
        // 2. Resamplear si es necesario
        val audio16k = if (sampleRate != 16000) {
            AudioResampler.resample(audioMono, sampleRate, 16000)
        } else {
            audioMono
        }
        
        // 3. Pasar a AntiDolbyController
        val app = context.applicationContext as IVANNAApplication
        app.antiDolbyController?.processAudioFrame(audio16k)
    }
}
```

---

## PASO 3: Requisitos de Formato

**AntiDolbyController espera**:
- ✅ Frecuencia de muestreo: **16000 Hz (16kHz)**
- ✅ Canales: **Mono (1 canal)**
- ✅ Tipo: **FloatArray normalizado [-1.0, 1.0]**
- ✅ Tamaño: **Cualquiera (buffer circular lo maneja)**

**Si tienes otro formato**:
- Estéreo @ 48kHz → Resamplear a 16kHz + mezclar a mono
- Estéreo @ 16kHz → Mezclar a mono
- Mono @ 48kHz → Resamplear a 16kHz

Usa `AudioResampler.kt` (ya existe en el proyecto):
```kotlin
val audio16k = AudioResampler.resample(audioData, 48000, 16000)
```

---

## PASO 4: Verificación en Logcat

Después de integrar, deberías ver:

```
D/AntiDolbyController: Yamnet: speech=0.250, music=0.500, bass=0.250, silence=0.000
D/AntiDolbyController: Yamnet: speech=0.100, music=0.800, bass=0.100, silence=0.000
D/AntiDolbyController: Yamnet: speech=0.600, music=0.200, bass=0.200, silence=0.000
```

**Si NO ves estos logs** → Nadie está llamando `processAudioFrame()`.

**Si ves ERROR**: "YamnetClassifier no disponible" → Modelo TFLite no se cargó (verificar assets/).

---

## PASO 5: Verificar Que Scores Llegan a C++

En el lado C++ (audio_orchestrator.cpp), debería recibir:

```cpp
void control_set_yamnet_scores(float voice, float music, float bass, float silence) {
    Log.d("AntiDolby", "Scores recibidos: voice=%.3f, music=%.3f, bass=%.3f, silence=%.3f",
          voice, music, bass, silence);
    
    g_control_frame.widener_multiplier = calculateWidenerFromScores(bass);
    g_control_frame.compressor_ratio = calculateCompressorFromScores(bass);
    // ... más ajustes dinámicos
}
```

**Logs C++ esperados**:
```
D/AudioOrchestrator: Scores recibidos: voice=0.500, music=0.300, bass=0.200, silence=0.000
D/WidenerDSP: Multiplier dinámico = 1.2 (bass dominante)
D/CompressorDSP: Ratio dinámico = 4:1 (bass dominante)
```

---

## CHECKLIST DE INTEGRACIÓN

- [ ] **PlaybackCaptureService actualizado** → Llama `antiDolbyController?.processAudioFrame()`
- [ ] **Audio resampleado a 16kHz** → YAMNet espera exactamente esto
- [ ] **Audio convertido a mono** → YAMNet espera 1 canal
- [ ] **Logs muestran clasificación** → "Yamnet: speech=..., music=..., bass=..."
- [ ] **Parámetros DSP cambian dinámicamente** → Exciter/Widener/EQ varían según contenido
- [ ] **C++ recibe scores** → `nativeSetAntiDolbyScoresStatic` llamada desde Kotlin
- [ ] **Modelo TFLite en assets** → `app/src/main/assets/yamnet.tflite` existe

---

## TROUBLESHOOTING

### Problema: "YamnetClassifier no disponible"
**Causa**: Modelo `yamnet.tflite` no está en assets  
**Solución**: 
```bash
cp yamnet.tflite app/src/main/assets/
```

### Problema: UnsatisfiedLinkError en AudioEngine
**Causa**: `libivanna_omega.so` no se compiló correctamente  
**Solución**: Verificar `CMakeLists.txt`, compilar NDK

### Problema: Scores siempre en cero
**Causa**: `processAudioFrame()` nunca se llama  
**Solución**: Verificar que PlaybackCaptureService/SystemAudioCapture integrados

### Problema: Latencia muy alta
**Causa**: Buffer de 0.96s es demasiado grande  
**Solución**: Reducir `YAMNET_INPUT_LENGTH` en `AntiDolbyController` a 4096 (256ms @ 16kHz)

---

## SIGUIENTE PASO: Fase 2

Después de integrar audio:

1. **Validar captura** → Logcat muestra clasificación
2. **Calibrar parámetros** → Ajustar thresholds (60% voz, 40% bajos)
3. **Optimizar latencia** → Reducir buffer si es necesario
4. **Pulir Dolby detection** → `AntiDolbyPreset.isDolbyPresent()` debería activar curva EQ inversa

---

## Compensación por ruta de salida (Bluetooth/AUX/USB)

Ver `AudioRouteManager.kt` + `audio_control_plane.hpp::control_set_route_profile()`.

**Mecanismo:** SBC/AAC recodifican con pérdida en la banda 2-4kHz (inteligibilidad
de diálogo) y colapsan parcialmente la imagen estéreo. `AudioRouteManager`
detecta la ruta activa vía `AudioDeviceCallback` y aplica tres parámetros al
pipeline (`route_bass_boost_db`, `route_dialog_boost_db`, `route_widener_mult`)
que se funden en `f.low`, `f.mid` y el ancho estéreo combinado respectivamente.

**Fallback por bitrate bajo:** cuando `AudioManager.getParameters("bt_codec_bitrate")`
reporta menos de 200kbps (API no pública/vendor-specific, sin garantía en todos
los OEM — se degrada a `null` sin romper nada si falla), se usa un perfil más
agresivo: `widenerMult=0.5`, `dialogBoostDb=4.5` en vez de `0.65`/`3.5`.

**Los valores de boost (3.5dB, 4.5dB, etc.) son heurísticas de partida, no
mediciones verificadas contra hardware real.** Antes de publicar cualquier
comparación cuantitativa contra otro sistema (Dolby u otro), se requiere:
1. Medición de latencia roundtrip real por ruta (herramienta tipo Oboe
   `LatencyTuner` con loopback físico, no simulado).
2. Medición de nivel de diálogo real (RMS banda 1-4kHz) en material de
   referencia, en ambos sistemas, con la misma fuente y mismo hardware de
   salida.
3. Ninguna de las dos mediciones anteriores se ha ejecutado todavía en este
   repositorio — no hay hardware de prueba ni cámara de alta velocidad
   disponibles en este entorno de desarrollo. Cualquier tabla de "ms" o "dB"
   publicada sin esa medición sería una afirmación falsa.
