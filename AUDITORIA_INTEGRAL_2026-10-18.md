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

- `SofaReader` y `Hdf5File` tienen pruebas JVM que comparan valores de archivos SOFA con referencia h5py, pero la búsqueda de `SofaReader.read` encontró solo pruebas: el parser Kotlin no se ejecuta en producción.
- **Sí existe una ruta HRTF medida de producción distinta:** `HRTFConvolver::init` busca datasets `.ihr1`, y `ObjectRenderer::loadHrtfDatasetFromFile` carga IHR1. `tools/sofa_to_ihr1.py` convierte los SOFA distribuidos a ese formato antes de su despliegue; el renderer puede consumir esas HRIR medidas cuando el archivo está presente y pasa validación.
- La ruta separada `SofaHRTFLoader.cpp` declara `VALID_NOT_PARSED`: valida firma HDF5, pero esa ruta analítica reconstruye HRIR desde PCA. No debe confundirse con el loader IHR1.
- Estado: **HRTF medido vía IHR1 está cableado; la importación directa de archivos .sofa arbitrarios en runtime no está implementada**. La selección de sujeto ahora verifica read-back nativo; aún requiere CI y prueba en dispositivo.

### 3. Sensado Wi-Fi / CSI / escena

- `WifiScanSession`, consentimiento y ciclo de vida ofrecen una ruta conservadora de metadatos Wi-Fi/RSSI.
- `UniversalWifiSensingAdapter` exige proveedores CSI registrados y verificados por hardware. La búsqueda de `registerProvider(` no encontró ningún registro de proveedor fuera de la propia definición.
- Estado: **metadatos Wi-Fi, sí; CSI real y reconstrucción geométrica métrica, no demostrados**. El fallback no debe presentarse como CSI ni como detección fiable de personas/arquitectura.

### 4. IA y redes neuronales

- Hay un artefacto TFLite real, `app/src/main/assets/anti_dolby_crnn.tflite`, usado por `AntiDolbyCrnnClassifier`; `YamnetClassifier` es una fachada de compatibilidad que delega en ese clasificador.
- `AIInferenceEngine.processAudioBlock` devuelve audio passthrough y no ejecuta inferencia TFLite. En la rama de auditoría se evita que `startSession` marque ese motor como activo y se deja el contador de inferencias en cero hasta que exista un ejecutor real. `ModelManager.hasUsableFineTuningModel()` sigue siendo `false`; ahora los archivos guardados se validan como FlatBuffer TFLite y se describen como no conectados a inferencia.
- `NeuralUpmixer` es el nombre histórico de una separación espectral heurística en C++/SIMD; su implementación no carga una red neuronal ni TFLite. Se corrigieron los comentarios de API para no llamarlo inferencia neuronal.
- `RealtimeLearningController` aplica sesgos EMA a parámetros y registra correcciones; eso no es entrenamiento de una red neuronal. El clasificador AntiDolby CRNN TFLite y el agente Gemini (cuando está configurado) son rutas separadas. No se deben mezclar estos estados.

### 5. Frontera de seguridad del daemon y memoria compartida

- El daemon root publicaba sockets UNIX de control y, en el socket principal, transfería el descriptor de memoria compartida a cualquier cliente que conectara y quedara silencioso durante el primer timeout. La política SELinux del módulo permite conexión desde `untrusted_app` a varios dominios para compatibilidad OEM; por sí sola no autentica al proceso cliente.
- En la rama de auditoría se añade verificación de `SO_PEERCRED` antes de aceptar comandos o transferir el descriptor: se permiten UID de la app instalada `com.ivanna.omega`, root y shell administrativo. La lista de UID de la app se descubre por usuario y se cachea con refresco acotado; los logs de rechazos se limitan para evitar llenar el log bajo spam.
- Se deshabilita el listener TCP opcional, que solo enlazaba a loopback pero no tenía autenticación de pares, y se restringe a `0600` cualquier socket UNIX basado en archivo.
- Se añadió una prueba CTest para la lista de UID autorizados. **La protección queda pendiente de compilación/CTest y de una prueba en dispositivo con el UID real de la app**; no se debe dar por cerrada antes de esos resultados.

### 6. Calibración y autonomía

- `RoomMeasurement` contiene una secuencia de medición, estimación de latencia y flujo de aplicar/verificar/revertir corrección; su lógica automática no demuestra que el micrófono del teléfono esté calibrado ni que el resultado mejore acústicamente todos los sistemas.
- `AutonomousAcousticTransaction.hpp` estaba en el árbol de producción, pero la búsqueda solo encontró su inclusión desde la prueba nativa. Se movió a `app/src/main/cpp/tests/support/` y se ajustó el include: queda explícito como soporte de pruebas, no como motor nativo productivo. La autonomía que sí existe en app es la ruta Kotlin; no se presenta la clase C++ de test como integración runtime.

### 7. Seguridad de configuración

- Se retiraron del manifest los permisos privilegiados `CAPTURE_AUDIO_OUTPUT`, `READ_LOGS` y `BIND_AUDIO_EFFECT_SERVICE`, que no tenían un uso de aplicación justificado en el código consultado. La captura de Ruta A usa MediaProjection con consentimiento.
- `app/build.gradle.kts` empaquetaba `GEMINI_API_KEY` en un campo BuildConfig. Aunque no se encontró un consumidor de ese campo, cualquier clave empaquetada en APK es extraíble. Se eliminó ese campo sin uso de la rama.
- `NetworkStatusPanel.kt` leía/escribía la clave en `SharedPreferences` sin cifrar pese a existir `SecureConfigurationManager`. En la rama se cambió a la configuración cifrada y se añadió migración de la clave heredada, eliminando la copia en texto plano.
- El backend web mantiene su propia variable de entorno `GEMINI_API_KEY`; no debe exponerse en cliente ni registrarse en logs.
- **Gobernanza:** la API de GitHub indicó que `main` no tenía protección de rama activada. El conector disponible no permite configurar esa protección desde esta auditoría; sin reglas de protección, un CI fallido no bloquea por sí solo un push directo.

### 8. Documentación y datos de prueba

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
