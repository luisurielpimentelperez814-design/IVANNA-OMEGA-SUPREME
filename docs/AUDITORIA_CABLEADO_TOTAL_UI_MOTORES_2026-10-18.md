# Auditoría integral de cableado UI → servicios → motor — 2026-10-18

## Alcance y criterio

Revisión estática del árbol `main` observado en el commit `25eddb0b089470a14ea2a885ccc047d90e72e8ca`. Se inspeccionaron el grafo principal de Compose Navigation, el panel CONTROL y sus callbacks, búsquedas de callbacks vacíos, rutas, puentes JNI y documentación de auditoría previa.

**No equivale a una prueba exhaustiva de cada control ni a validación de dispositivo.** No se dispuso de instrumentación física ni de resultados finales de CI en esta revisión. Los estados siguientes distinguen código observado de comportamiento aún no demostrado.

## Dictamen

La aplicación tiene varias cadenas de integración reales: controles principales actualizan estado y llaman JNI; navegación de pestañas usa un NavHost interno y subpantallas un controlador externo; controles de sala guardan configuración y llaman la ruta de aplicación; y varios comandos adaptativos actualizan estado y motor. Sin embargo, la arquitectura también usa callbacks Kotlin con valores predeterminados vacíos, así que la existencia de un componente o firma no demuestra que cada punto de entrada esté conectado en cada call-site.

El objetivo de esta auditoría es impedir tres clases de defectos:
1. **Control huérfano:** la UI acepta una acción pero no llega a servicio/motor.
2. **Estado ficticio o divergente:** UI/persistencia dicen ON o “aplicado” sin read-back/confirmación del subsistema.
3. **Destino inválido o desconectado:** botón navega a una ruta que no está registrada en el NavHost correcto, o se usa el controlador equivocado.

## Grafo principal observado

- Inicio de la aplicación: `MainActivity` usa un NavHost externo que comienza en `splash`.
- Interfaz principal: `MainScaffold` crea su propio NavController y un NavHost interno con pestañas `tab_control`, `tab_brain`, `tab_adaptive`, `tab_spatial`, `tab_system`.
- CONTROL: `MainScaffold → ControlTabScreen → IvannaControlPanel`.
- CONTROL delega subpantallas mediante `outerNav`; las pestañas BRAIN/ADAPTIVE/SPATIAL/SYSTEM usan `tabNav`. Este diseño dual requiere verificar cada destino contra el grafo propietario; no basta con que el nombre de ruta exista en otro NavHost.
- En el código inspeccionado, `onOpenHiRes` se pasa desde `ControlTabScreen` como `outerNav.navigate("hires")`; por tanto, ya no queda en el valor predeterminado vacío para ese call-site.

## Matriz de controles críticos inspeccionados

| Control / subsistema | Ruta observada | Persistencia / retorno | Estado de auditoría |
|---|---|---|---|
| Exciter, EQ, ancho estéreo, compresor | `IvannaControlPanel → ControlTabScreen → DSPState.pushToNative()` | Estado en memoria y envío JNI | Conexión de código observada; efecto acústico no medido |
| Armónicos NHO | Callback → `nativeSetHarmonicGain` si JNI está cargado | No se verificó read-back del valor | Parcial: comprobar estado efectivo y fallos |
| EVO | Callback → iniciar/detener hilo nativo mediante wrapper guardado | El estado inicial se restaura desde ParameterStore | Conexión observada; ciclo de vida/estado de hilo requiere prueba |
| NPE / PI-LSTM | Callbacks a `PiLstmBridge`, con bypass y comprobación de disponibilidad | Bypass se persiste | Parcial: cuando bridge no está listo, algunos cambios se omiten; UI debe reflejar no disponible |
| Espacialización | Ángulo y ancho llegan a JNI y `IvannaSpatialEngine` | Persistencia no demostrada para todos los campos en este call-site | Conexión observada; verificar read-back y conflicto con ThermalGovernor |
| Anti-Dolby | Controlador inicializado; toggle llama enable/disable | Se consulta ParameterStore al crear el panel | Conexión observada; confirmar resultado real del controlador |
| Presets | Nombre → búsqueda en `ProfilesLoader` → actualiza DSP si encuentra coincidencia | No se confirmó en el fragmento leído el guardado del preset seleccionado | Riesgo: nombre inexistente no produce cambio visible; agregar feedback |
| Auto mode / Omega mode | ParameterStore + AudioStateManager / OmegaEngineBridge | Persistencia observada en el callback | Conexión de código observada; probar coherencia UI↔motor |
| Phase Oracle | Estado global + `nativeSetPhaseParameters` | Estado actualizado | Conexión observada; verificar read-back y rangos |
| Adaptativo | Estado global + `nativeSetAdaptiveControls`; modo manual también aplica `adaptiveBack` | Estado global | Conexión observada; verificar latencia y modo manual/automático |
| HOA upmixing | ParameterStore + JNI + mensaje `SET_PERCEPTUAL_STATE` | Persistencia observada | Conexión observada; comprobar respuesta del bus/daemon |
| WFS | ParameterStore + JNI para enable/spread | Persistencia observada | Conexión observada; confirmar que snapshot/ruta de reproducción consume el estado |
| Punch | ParameterStore + JNI | Persistencia observada | Conexión observada; validar límites y read-back |
| Campo auxiliar | ParameterStore + JNI, límites en crossover/coherencia | Persistencia observada | Conexión observada; validar motor y ruta de salida |
| Campo temporal | ParameterStore + JNI, cantidad limitada | Persistencia observada | Conexión observada; validar consumo real en DSP |
| Haptics graves | ParameterStore + `BassHaptics.configure` | Persistencia observada | Parcial: probar soporte de dispositivo y estado efectivo |
| Corrección de sala | Permiso de micrófono → medición; toggle/clear → ParameterStore + `applyRoom` | Ganancias/estado guardados | Conexión de código observada; requiere medición de hardware calibrada |
| Navegación Hi-Res / visualizador / perfiles / Magisk / ejes | Callbacks explícitos en `ControlTabScreen` hacia `outerNav` | No aplica | Revisar cada destino contra NavHost externo y pruebas de navegación |

## Hallazgos que requieren cierre

### P0 — no declarar completitud hasta cerrar
- **CI de commit exacto:** el estado consultado del commit más reciente no devolvió checks de estado final; el workflow citado previamente estaba en cola. No hay evidencia suficiente para certificar build verde.
- **Grafo dual de navegación:** generar un inventario automático de cada llamada `navigate(...)`, y comprobar que la ruta está registrada en el NavHost correcto. Rutas que aparecen en un grafo distinto no cuentan.
- **Callbacks predeterminados vacíos:** auditar firmas y todos los call-sites, clasificando cada lambda vacía como intencional, sobreescrita por caller o defecto real. No reemplazar defaults globalmente sin revisar contratos de previews/tests.
- **Estado efectivo vs solicitado:** controles que muestran un estado ON deben leerlo de motor/servicio o mostrar claramente “solicitado / no confirmado”. No asumir éxito por haber enviado JNI/daemon.
- **Conflictos de escritor DSP:** controles de ancho espacial comparten zona de influencia con políticas térmicas; definir una única autoridad/arbiter para que una política no sobrescriba silenciosamente a la otra.

### P1 — cobertura transversal
- Crear tests de navegación para rutas principales y subpantallas.
- Crear tests de contrato para callbacks de cada pantalla y comprobar que cada control emite una acción identificable.
- Para cada puente JNI: verificar símbolo, rango, respuesta ante librería ausente y read-back cuando exista.
- Para cada parámetro persistente: probar guardar → destruir pantalla/proceso → restaurar → aplicar al motor.
- Para servicios con permisos: probar concedido, denegado, “no volver a preguntar”, revocación en Ajustes, cancelación y reentrada.
- Telemetría: distinguir muestra válida/reciente, muestra obsoleta, sin proveedor y fallo de lectura; nunca rellenar con una lectura sintética que parezca real.
- Auditar accesibilidad, estados disabled, carga, error y confirmación para todos los botones de acción.
- Medir regresión de audio con señal de referencia: clipping, picos, latencia, underruns, respuesta de frecuencia y A/B; no inferir mejora subjetiva de un test de compilación.

## Protocolo de cierre para cada control

Cada control se marca como “cerrado” solo con evidencia para todos los pasos:

1. UI emite un evento identificable.
2. Callback correcto recibe valor validado y normalizado.
3. Estado fuente de verdad se actualiza.
4. Persistencia se escribe si el ajuste debe sobrevivir reinicio.
5. Servicio/bridge/motor recibe el valor.
6. El receptor confirma aplicación o lectura posterior coincide.
7. UI refleja éxito, fallo o indisponibilidad sin afirmar éxito prematuro.
8. Test de regresión cubre camino feliz, error, cancelación y reentrada.

## Artefactos que deben mantenerse sincronizados

- `AGENT_CLAIMS.md`: coordinación de flancos antes de editar código.
- `AUDITORIA_INTEGRAL_2026-10-18.md`: límites conocidos del producto y resultados por subsistema.
- `docs/UI_WIRING_AUDIT.md`: histórico; su tabla original data del 2026-09-20 y no debe usarse como prueba del estado actual.
- `PRODUCT_MASTER_STATUS.md`: debe actualizarse solo con resultados del commit verificado, no con aspiraciones.

## Cierre honesto

Esta revisión identifica rutas y controles concretos, pero no certifica todavía el cableado total de las 253 clases Kotlin ni cada uno de los 2,403 archivos del inventario histórico. La siguiente fase técnica debe automatizar el inventario del grafo de navegación y los callbacks, ejecutar CI en el commit resultante y cubrir los casos faltantes con pruebas. No se deben declarar todos los controles conectados, el APK listo ni el audio validado hasta que existan esos resultados.
