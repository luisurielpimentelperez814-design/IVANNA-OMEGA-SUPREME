# GUÍA DE INTEGRACIÓN — ARQUITECTURA MAGISTRAL DE AUDIO

## RESUMEN DE COMPONENTES

### 1. AudioStateManager (Fuente única de verdad)
- **Centraliza**: Modo, intensidad, parámetros de compressor/exciter/EQ
- **Proporciona**: LiveData + StateFlow para reactividad
- **Expone**: `updateState()`, `getDeltaChanges()`, `resetToDefaults()`

```kotlin
AudioStateManager.updateState {
    it.copy(adaptiveIntensity = 0.8f)
}
```

### 2. ParameterStore (Persistencia avanzada)
- **Versionado**: schema_version para migración futura
- **Debounce**: Espera 500ms de inactividad antes de guardar
- **Carga**: Con transición suave de 300ms

```kotlin
val store = ParameterStore(context)
store.saveParametersDebounced(state)  // Guarda con debounce
val loaded = store.loadParameters()   // Carga del disco
```

### 3. AudioParameterManager (Transiciones suaves)
- **ValueAnimator**: Interpola entre estados en 300ms
- **Callback**: onUpdate llamado en cada frame
- **Previene**: Saltos bruscos que causan clics en audio

```kotlin
manager.applyParametersWithTransition(
    fromState, toState, 300L
) { interpolated ->
    applyToEngine(interpolated)
}
```

### 4. DspStateUpdater (Delta updates + debounce)
- **Delta**: Solo envía parámetros que cambiaron
- **Sincronización**: @50ms (ciclo de audio)
- **Debounce**: 24ms para slider coalescing

```kotlin
updater.requestUpdate(newState)    // Con debounce
updater.forceUpdate(newState)      // Inmediato
```

### 5. AdaptiveEngineModulator (Curvas no lineales)
- **Mapeo**: Intensidad → factor usando pow()
- **Soft clipping**: tanh() para evitar distorsión
- **Suavizado**: Filtro exponencial (α=0.2)

```kotlin
val modulatedState = modulator.modulateAdaptiveOutput(
    baseState, mode, intensity
)
```

### 6. VoiceProtectionManager (Protección de voz)
- **Restauración**: Carga estado correcto al iniciar app
- **Callbacks**: Aplica cuando bridgePlayer esté listo
- **Indicadores**: Badge visual en UI

```kotlin
manager.loadAndRestore()
manager.registerBridgePlayer(player)
```

## FLUJO DE DATOS

```
UI (Sliders)
    ↓
DspStateUpdater.requestUpdate(newState)
    ↓ (debounce 24ms)
AudioStateManager.updateState()
    ↓ (computa deltas)
getDeltaChanges()
    ↓ (solo deltas)
AudioParameterManager.applyParametersWithTransition()
    ↓ (300ms transición)
AdaptiveEngineModulator.modulateAdaptiveOutput()
    ↓ (curvas + soft clip + suavizado)
ParameterStore.saveParametersDebounced()
    ↓ (debounce 500ms)
SharedPreferences (disco)
```

Y en paralelo:

```
Audio Loop (@50ms)
    ↓
Leer AudioStateManager.audioState
    ↓
Aplicar a engine nativo
    ↓
Reproducir audio modificado
```

## IMPLEMENTACIÓN EN MainActivity

### 1. En onCreate()

```kotlin
override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    
    // Inicializar componentes
    val store = ParameterStore(this)
    val voiceManager = VoiceProtectionManager(store)
    val paramManager = AudioParameterManager()
    val updater = DspStateUpdater()
    val modulator = AdaptiveEngineModulator()
    
    // Cargar parámetros persistidos
    val savedState = store.loadParameters()
    
    // Aplicar con transición suave
    paramManager.applyParametersWithTransition(
        AudioState(),
        savedState,
        300L
    ) { interpolated ->
        AudioStateManager.updateState { interpolated }
    }
    
    // Restaurar voice protection
    voiceManager.loadAndRestore()
    
    // Observar cambios en estado
    AudioStateManager.audioState.collect { newState ->
        // Aplicar modulación
        val modulatedState = modulator.modulateAdaptiveOutput(
            newState,
            newState.adaptiveMode,
            newState.adaptiveIntensity
        )
        
        // Enviar actualizaciones al engine
        updater.requestUpdate(modulatedState)
        
        // Guardar con debounce
        store.saveParametersDebounced(modulatedState)
    }
}
```

### 2. En sliders

```kotlin
Slider(
    value = state.adaptiveIntensity,
    onValueChange = { newIntensity ->
        AudioStateManager.updateState {
            it.copy(adaptiveIntensity = newIntensity)
        }
        // Automáticamente:
        // - Se calcula delta
        // - Se debounce (24ms)
        // - Se aplica transición (300ms)
        // - Se modula
        // - Se guarda con debounce (500ms)
    }
)
```

### 3. Voice Protection Toggle

```kotlin
Switch(
    checked = voiceManager.isActive(),
    onCheckedChange = { enabled ->
        if (enabled) {
            voiceManager.enable()
        } else {
            voiceManager.disable()
        }
        // El estado se persiste automáticamente
    }
)
```

## PRUEBAS

Ejecutar todos los tests:

```bash
./gradlew test
```

Tests individuales:

```bash
./gradlew test --tests AudioStateManagerTest
./gradlew test --tests DspStateUpdaterTest
./gradlew test --tests AdaptiveEngineModulatorTest
```

## RENDIMIENTO

- **Audio loop**: Sin bloqueos (todo async)
- **Slider response**: 24ms (imperceptible)
- **Persistencia**: No bloquea main thread (debounce + corrutinas)
- **Transiciones**: Suaves @ 300ms (no distorsión)
- **Modulación**: <1ms (cálculos simples)

## LOGGING

Todos los cambios se loguean con Log.d() en TAG específicos:

- `AudioStateManager` - Cambios de estado
- `ParameterStore` - Persistencia
- `DspStateUpdater` - Actualizaciones al DSP
- `AdaptiveEngineModulator` - Modulación
- `VoiceProtectionManager` - Voice protection

Buscar en logcat:

```bash
adb logcat | grep "AudioState\|ParameterStore\|DspState\|Modulator\|VoiceProtection"
```
