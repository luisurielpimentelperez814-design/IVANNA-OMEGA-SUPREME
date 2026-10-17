# Mapa de piezas élite existentes y función prevista

| Pieza existente | Papel que se debe reutilizar | Criterio de integración |
|---|---|---|
| `SpatialAudioPanel.kt` | UI y estado para HRTF/RIR/SAF | Ampliar la pantalla actual; evitar panel duplicado |
| `IvannaSpatialEngine` / `IvannaSpatialManager` | Motor/gestión espacial Android | Confirmar setter y consumidor antes de conectar |
| `SaFBridge` / `SaFOptimizer` | Calibración SAF | Validar entradas, convergencia y salida finita |
| `RirConvolver` / dataset SOFA-SAF-RIR | Convolución de sala y datos espaciales | Verificar tasa de muestreo, particionado y latencia |
| `HearingAdaptationEngine` | Adaptación auditiva | No inferir perfiles médicos ni exagerar el resultado |
| `SnnNmfHoaUpmixer` | Upmix / HOA | Proteger compatibilidad mono, estéreo y contenido ya espacializado |
| `PinnaManifoldInterpolator` | Interpolación de pistas de pabellón auricular | Interpolar dentro del dominio de datos validado |
| `PhaseCoherentTransharmonicSynthesizer` | Síntesis transarmónica | Activación opcional; A/B y control de distorsión |
| `WarpedLatticeTransducerInverter` | Compensación/modelo de transductor | Solo con modelo y límites adecuados al dispositivo/salida |
| `SupremeTransitionEnvelope` | Transiciones suaves | Evitar clics y saltos al cambiar de perfil/ruta |
| `SupremeAcousticStabilityGuard` | Guardas de estabilidad | Failsafe con bypass seguro ante valores inválidos |
| `SafetyLimiter` | Limitación de nivel | Mantener antes de la salida final; medir pico real |
| `rt_band_meter` / omega effect beacon | Telemetría | Mostrar estado medido y diferenciarlo del valor pedido |
| `RouteArbiter` | Exclusión entre Ruta A y Ruta B | Una ruta de procesamiento espacial activa a la vez |
| `command_server.cpp` | Control del demonio | Añadir únicamente comandos presentes y tipados |
| DEH6 angular-spectrum / Gerchberg–Saxton | Investigación de propagación/reconstrucción óptica | Fuera del callback; no es un efecto de audio |

La presencia de estos nombres en el código no prueba por sí sola la ruta de ejecución. Antes de exponer un control, rastrear el símbolo desde el setter hasta el bloque que procesa muestras.
