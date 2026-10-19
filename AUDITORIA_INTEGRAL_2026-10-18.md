# Auditoría integral en caliente — IVANNA OMEGA SUPREME

**Fecha de auditoría:** 2026-10-18 (UTC de los workflows: 2026-10-19)  
**Baseline inspeccionado:** `main` en `953620d8b1c0b988b0ff240e0c980cf1ebb4fa49`  
**Rama de correcciones:** `audit/hot-integral-2026-10-18`  
**Alcance:** inspección estática de estructura, rutas de UI/servicios, Kotlin/JNI/C++, CI, HRTF/SOFA, sensado Wi-Fi, modelos IA, configuración, documentación y artefactos. No se tuvo un teléfono físico conectado; por tanto, no se afirma validación acústica de hardware.

## Dictamen ejecutivo

**El baseline no estaba listo para declararse entregable.** El test nativo CTest pasó, pero la compilación Android falló y una segunda puerta de auditoría detectó discrepancias de cableado/documentación. Además, hay funciones cuyo código existe sin participar en la ruta de reproducción real. Se corrigieron en esta rama los errores de compilación identificados, los falsos positivos conocidos del auditor, la documentación obsoleta que rompía el gate y dos problemas de manejo de claves. La aceptación queda condicionada al CI de esta rama.

## Inventario observado

En el árbol Git consultado se encontraron 2,403 archivos: 253 archivos Kotlin/Android, 619 C++/nativo, 481 del módulo Magisk, 30 de FotoHolo web, 81 documentos, 14 scripts de auditoría/build y 3 workflows principales. Se detectaron 40 pantallas/paneles Kotlin por patrón de nombre y 169 archivos de prueba por patrón de nombre; estas cifras son inventario de archivos, no porcentaje de cobertura funcional.

Versión declarada en `version.properties` y `magisk_module/module.prop`: **v2.5.74, versionCode 365**. La coherencia de versión no demuestra por sí sola que el APK del mismo commit se haya producido correctamente.

## Resultados comprobados del baseline

| Comprobación | Resultado | Evidencia |
|---|---|---|
| CTest nativo | **412/412 aprobados** en la ejecución consultada | [Workflow CI](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions/runs/39186104752) |
| Compilación Release Kotlin/APK | **Falló** en `main` | Mismo workflow; tarea `:app:compileReleaseKotlin` |
| Puerta adicional host (cableado/afirmaciones) | **Falló** | [Workflow CTest host](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions/runs/39186104749) |
| Publicación de APK validado para ese commit | **No confirmada**; el build falló antes de empaquetar/verificar | Workflow de CI |
| A/B acústico, latencia y underruns en teléfono | **No verificados en dispositivo** | Requiere hardware y medición controlada |

### Errores de compilación reproducidos desde el log

- `SaFEngine.kt`: dos `companion object`, declaraciones conflictivas y `TAG` sin resolver.
- `CsiFeatureExtractor.kt`: conversión inválida de `List<Double>` a `FloatArray`.
- `WifiSensingAutoStart.kt`: import de `LocalLifecycleOwner` no disponible en la versión Compose usada.

En la rama de auditoría se fusionó `TAG` en el único companion, se convirtió explícitamente cada amplitud a `Float` y se cambió el import al paquete Compose compatible. **No se considera corregido hasta que el workflow compile el APK.**

## Hallazgos por subsistema

### 1. UI, controles y puentes nativos

- El repositorio tiene puertas estáticas para cableado JNI, wrappers Kotlin, headers, RT-safety, flags de compilación y afirmaciones documentales.
- Tres métodos públicos de `FotoHoloTelemetryBridge` están anotados `@JavascriptInterface` y se invocan desde JavaScript/WebView; el auditor solo buscaba llamadores Kotlin y los marcaba erróneamente como huérfanos. Se corrigió la regla para no tratar esos puntos de entrada externos como wrappers sin consumidor Kotlin.
- La presencia de una llamada JNI no demuestra por sí sola que el audio final cambie. Los mensajes de estado deben derivarse del resultado/read-back real del motor, no de la mera emisión de un comando.

### 2. HRTF / SOFA

- `SofaReader` y `Hdf5File` existen y tienen pruebas JVM que comparan dimensiones/valores de archivos SOFA con resultados de referencia h5py.
- **Brecha de integración confirmada:** la búsqueda de usos de `SofaReader.read` encontró solo pruebas, no una llamada de producción.
- `SofaHRTFLoader.cpp` declara explícitamente `VALID_NOT_PARSED`: verifica la firma HDF5, pero el HRIR que usa esa ruta se reconstruye desde la base PCA. Por tanto, validar o incluir el archivo `.sofa` no significa que sus mediciones alimenten la convolución.
- Estado: lector probado como parser; **SOFA no conectado a la ruta de reproducción real**. No se afirma mejora audible por este lector.

### 3. Sensado Wi-Fi / CSI / escena

- `WifiScanSession`, consentimiento y ciclo de vida ofrecen una ruta conservadora de metadatos Wi-Fi/RSSI.
- `UniversalWifiSensingAdapter` exige proveedores CSI registrados y verificados por hardware. La búsqueda de `registerProvider(` no encontró ningún registro de proveedor fuera de la propia definición.
- Estado: **metadatos Wi-Fi, sí; CSI real y reconstrucción geométrica métrica, no demostrados**. El fallback no debe presentarse como CSI ni como detección fiable de personas/arquitectura.

### 4. IA y redes neuronales

- Hay un artefacto TFLite real, `app/src/main/assets/anti_dolby_crnn.tflite`, usado por `AntiDolbyCrnnClassifier`; `YamnetClassifier` es una fachada de compatibilidad que delega en ese clasificador.
- `AIInferenceEngine.processAudioBlock` actualmente devuelve una copia sin modificar del audio y no ejecuta inferencia TFLite. `ModelManager.hasUsableFineTuningModel()` devuelve siempre `false`; el guardado de modelos finamente ajustados no equivale a entrenamiento ni inferencia activa.
- `RealtimeLearningController` aplica sesgos EMA a parámetros y registra correcciones; eso no es entrenamiento de una red neuronal.
- Estado: existe un clasificador TFLite específico; **no hay evidencia de que el motor genérico de fine-tuning/inferencia esté conectado a la señal de audio**. No se deben mezclar ambos estados.

### 5. Calibración y autonomía

- `RoomMeasurement` contiene una secuencia de medición, estimación de latencia y flujo de aplicar/verificar/revertir corrección; su lógica automática no demuestra que el micrófono del teléfono esté calibrado ni que el resultado mejore acústicamente todos los sistemas.
- `AutonomousAcousticTransaction.hpp` estaba en el árbol de producción, pero la búsqueda solo encontró su inclusión desde la prueba nativa. Se movió a `app/src/main/cpp/tests/support/` y se ajustó el include: queda explícito como soporte de pruebas, no como motor nativo productivo. La autonomía que sí existe en app es la ruta Kotlin; no se presenta la clase C++ de test como integración runtime.

### 6. Seguridad de configuración

- `app/build.gradle.kts` empaquetaba `GEMINI_API_KEY` en un campo BuildConfig. Aunque no se encontró un consumidor de ese campo, cualquier clave empaquetada en APK es extraíble. Se eliminó ese campo sin uso de la rama.
- `NetworkStatusPanel.kt` leía/escribía la clave en `SharedPreferences` sin cifrar pese a existir `SecureConfigurationManager`. En la rama se cambió a la configuración cifrada y se añadió migración de la clave heredada, eliminando la copia en texto plano.
- El backend web mantiene su propia variable de entorno `GEMINI_API_KEY`; no debe exponerse en cliente ni registrarse en logs.
- **Gobernanza:** la API de GitHub indicó que `main` no tenía protección de rama activada. El conector disponible no permite configurar esa protección desde esta auditoría; sin reglas de protección, un CI fallido no bloquea por sí solo un push directo.

### 7. Documentación y datos de prueba

- `PRODUCT_MASTER_STATUS.md` decía v2.3.16, C++23 y 150 tests, datos incompatibles con la versión actual v2.5.74, el estándar C++20 y el inventario de CTest actual. Se reemplaza por un estado anclado a evidencia y este informe.
- `LÉAME.md` conservaba cifras/versiones antiguas y rompía la puerta documental; se convirtió en una página de compatibilidad que remite a `LEAME.md` y `README.md`.
- El resumen de CTest y las afirmaciones de pruebas deben derivarse de logs de una ejecución identificada. No se deben mezclar resultados de commits diferentes ni declarar un porcentaje global de cobertura a partir del conteo de tests.

## Correcciones incluidas en esta rama

1. Fix de compilación de SaF, conversión CSI y paquete de `LocalLifecycleOwner`.
2. Auditor de wrappers: reconoce `@JavascriptInterface` como entrada externa válida.
3. Elimina el encabezado C++ de prueba del directorio de producción y corrige el include del test.
4. Retira cifras/versiones obsoletas de `LÉAME.md`.
5. Elimina el campo BuildConfig de API key y migra preferencias de clave a almacenamiento cifrado.

Commits de la rama: `0ae0249c`, `7533c639`, `41caaedc` (el SHA más reciente puede cambiar si se incorporan nuevas correcciones).

## Criterios de aceptación

La auditoría no se considera cerrada hasta que:

- el workflow de esta rama compile Kotlin y genere el APK;
- CTest y todas las puertas estáticas pasen en el mismo commit;
- el APK y módulo se verifiquen por hash/estructura desde artefactos de ese commit;
- se haga una prueba física de audio con bypass y nivel igualado, registrando clipping, underruns, CPU y latencia;
- se conecte SOFA a la ruta de convolución si se quiere afirmar que se reproducen HRIR leídas del archivo;
- se incorpore un proveedor CSI real y verificado antes de afirmar sensado CSI en hardware.

**Conclusión:** se corrigieron fallos concretos y riesgos de configuración en la rama de auditoría, pero no sería honesto declarar «cero brechas»: la integración de SOFA, el proveedor CSI, la inferencia genérica y la validación acústica física siguen sin evidencia end-to-end. Esas limitaciones son resultados de la auditoría, no funciones que deban darse por hechas.
