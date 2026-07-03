# Anti-Dolby: Próximos Pasos (Phase 2)

**Estado actual**: Anti-Dolby está reparado pero sin audio real. Es como tener un motor listo pero sin combustible.

---

## FASE 1 ✅ COMPLETADA

- [x] Corregir índices de YAMNet
- [x] Crear AntiDolbyController
- [x] Inicializar AudioEngine
- [x] Conectar JNI `nativeSetAntiDolbyScoresStatic`
- [x] Documentar todo

**Resultado**: Anti-Dolby listo para recibir audio, procesar y adaptar DSP.

---

## FASE 2 ⏳ INMEDIATA

### Paso 1: Conectar PlaybackCaptureService

**Archivo**: `app/src/main/java/com/ivanna/omega/audio/PlaybackCaptureService.kt`

Busca el método `onAudioAvailable` o `processAudioFrame`:

```kotlin
// AGREGAR ESTO:
private fun feedAntiDolby(audioFrame: FloatArray, sampleRate: Int) {
    val app = context.applicationContext as? IVANNAApplication ?: return
    
    // Resamplear a 16kHz si es necesario
    val audio16k = if (sampleRate != 16000) {
        AudioResampler.resample(audioFrame, sampleRate, 16000)
    } else {
        audioFrame
    }
    
    // Mezclar a mono si es estéreo
    val audioMono = if (audio16k.size > 16000) {
        FloatArray(audio16k.size / 2) { i ->
            (audio16k[i * 2] + audio16k[i * 2 + 1]) / 2f
        }
    } else {
        audio16k
    }
    
    // Pasar a AntiDolby
    app.antiDolbyController?.processAudioFrame(audioMono)
}
```

Luego llama `feedAntiDolby(audio, sampleRate)` en cada frame.

### Paso 2: Verificar Logs

```bash
adb logcat | grep "AntiDolbyController"
```

Deberías ver:
```
D/AntiDolbyController: Yamnet: speech=0.100, music=0.800, bass=0.100, silence=0.000
D/AntiDolbyController: Yamnet: speech=0.500, music=0.300, bass=0.200, silence=0.000
```

Si **no ves nada** → Audio no está siendo pasado a `processAudioFrame()`.

### Paso 3: Verificar DSP

Parámetros que deberían cambiar dinámicamente:
- `exciter` (0.0-1.0)
- `width` (0.0-1.0)
- `eq_gain_db` (-18 a +18)

Logs esperados:
```
D/AntiDolbyController: Yamnet: speech=0.250, music=0.600, bass=0.150, silence=0.000
D/AudioEngine: setExciter(0.6) // Music domina
D/AudioEngine: setWidth(0.7)
D/AudioEngine: setEqGain(3.0)
```

---

## ARCHIVO CLAVE: PlaybackCaptureService.kt

**Ubicación**: `app/src/main/java/com/ivanna/omega/audio/PlaybackCaptureService.kt`

**Qué buscar**:
1. Método que recibe audio (`onAudioAvailable`, `processAudioFrame`, etc.)
2. Sample rate del audio
3. Formato del audio (mono/estéreo)
4. Dónde se procesa actualmente

**Qué modificar**:
1. Resamplear a 16kHz (si no lo está)
2. Mezclar a mono (si es estéreo)
3. Llamar `antiDolbyController?.processAudioFrame(audio16kMono)`

---

## CHECKLIST PHASE 2

- [ ] PlaybackCaptureService modificado
- [ ] Audio resampleado a 16kHz
- [ ] Audio convertido a mono
- [ ] Llamada a `antiDolbyController?.processAudioFrame()`
- [ ] Logcat muestra clasificación YAMNet
- [ ] Parámetros DSP cambian dinámicamente
- [ ] Sonido de reproducción se adapta según contenido

---

## TROUBLESHOOTING

**Problema**: No veo logs de YAMNet
→ Verificar que `processAudioFrame()` se está llamando realmente

**Problema**: Logs pero parámetros no cambian
→ Verificar que JNI `nativeSetAntiDolbyScoresStatic` se llama desde C++

**Problema**: Latencia muy alta
→ Reducir buffer de 0.96s a 256ms en `AntiDolbyController`

---

## CONTACTO/SOPORTE

Código está documentado con:
- Comentarios claros en cada función
- Logs DEBUG en puntos críticos
- Fallbacks graceful si algo falla

Ver `ANTI_DOLBY_INTEGRATION_GUIDE.md` para detalles completos.
