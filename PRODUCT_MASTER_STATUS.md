# Estado maestro del producto — IVANNA OMEGA SUPREME

**Última revisión:** 2026-10-22  
**Versión declarada en `version.properties`:** v2.5.124 · versionCode 415. La coherencia con el módulo y los artefactos del último commit debe confirmarse en CI.  
**Snapshot remoto consultado:** `main` en `a4892db8c25e8c2d20e9b684c8f994307c453d50` (2026-10-22).  
**Auditoría integral en caliente:** [AUDITORIA_INTEGRAL_2026-10-18.md](AUDITORIA_INTEGRAL_2026-10-18.md)

## Estado verificado

| Área | Estado observado |
|---|---|
| Android | minSdk 28 · targetSdk 35 · JDK 17 · Kotlin/JNI · C++20 · NDK 26.1.10909125 |
| Versión declarada | `version.properties` declara v2.5.124 (415); el artefacto validado del mismo commit no queda demostrado hasta finalizar CI |
| CTest nativo | 412/412 tests pasaron en el workflow consultado del baseline |
| Compilación Release del APK en baseline | Falló en `SaFEngine.kt`, `CsiFeatureExtractor.kt` y `WifiSensingAutoStart.kt` |
| Puertas estáticas del baseline | Fallaron por wrappers JavaScript mal clasificados, un header de test bajo producción y una cifra documental obsoleta |
| Rama de auditoría | Contiene correcciones para los hallazgos anteriores y para el manejo de claves; aceptación pendiente de CI |
| HRTF/SOFA | Ruta HRTF medida vía IHR1 sí existe y el renderer la carga; el parser Kotlin de SOFA es solo de pruebas. La ruta separada SofaHRTFLoader valida firma y usa PCA, no mediciones del SOFA crudo |
| Wi-Fi/CSI | Fallback de metadatos RSSI; no se encontró un proveedor CSI registrado/verificado |
| IA genérica | El clasificador CRNN TFLite existe; `AIInferenceEngine` queda explícitamente inactivo/passthrough; el upmixer es heurístico y el fine-tuning no está conectado a inferencia real |
| Audio en hardware | Latencia, underruns, clipping, THD y mejora A/B no verificados en un teléfono durante esta auditoría |
| Daemon root | La rama de auditoría añade `SO_PEERCRED` en ambos sockets UNIX antes de comandos/SHM, deshabilita TCP sin autenticación y limita logs de rechazos; CI y prueba en dispositivo pendientes |\n| Protección de `main` | GitHub informó que la rama no está protegida; el CI no bloquea por sí solo pushes directos |

## Regla de estado

No usar «100 % listo», «cero brechas», «mejor sonido demostrado» ni «CI verde» sin evidencia del mismo commit. El estado del artefacto debe provenir del workflow y sus hashes; la mejora de sonido requiere una prueba física controlada.



## Sincronización remota — 2026-10-22

- **Commit consultado en `main`:** `a4892db8c25e8c2d20e9b684c8f994307c453d50` — v2.5.124, versionCode 415; cambio de identidad estricta de ruta Bluetooth en el reproductor propio.
- **CI del cambio más reciente:** el job nativo CTest terminó correctamente; el job de compilación/paquetización APK y binarios seguía en ejecución en la consulta. No declarar build verde ni release validada hasta que finalice y se inspeccionen los artefactos.
- **PR #137 (BluetoothAudioProfiler):** abierto; CTest del commit de cabeza pasó, compilación Android en curso. La medición A2DP física sigue pendiente.
- **PR #86 (captura espacial ARCore):** abierto. La implementación documenta nube de puntos estimada y exportación PLY/JSON; no equivale a malla cerrada, reconstrucción fotorrealista 4K ni calibración acústica.
- **HAL/PAL y reloj:** `hal_integration/MOTO_G85_PAL_CLOCK_INTEGRATION.md` mantiene los callbacks de consulta de muestra hardware emparejada y aplicación ASRC real sin enlazar. El código de integración y las pruebas con dobles no prueban una conexión de hardware.
- **Publicación:** la consulta de los cinco releases más recientes devolvió borradores de staging; no se confirmó un lanzamiento público correspondiente a v2.5.124.
- **Política de entrega:** no activar corrección de deriva mediante IDs/controles no verificados; no declarar inferencia IA local completa, reconstrucción 3D fotorrealista ni mejora acústica medida sin evidencia específica.



## ASRC software propio — 2026-10-22

- El ASRC de reproducción de la app es `StereoAudioResampler.kt`: FIR sinc Blackman–Harris de 64 taps/1024 fases, fase e historial persistentes entre bloques, y corrección de reloj acotada a ±500 ppm.
- `IvannaBridgePlayer.kt` alimenta el servo con pares de posición de frame/timestamp de una misma consulta `AudioTrack.getTimestamp()`; si la consulta falla, vuelve a corrección cero. Este alcance es la pista de reproducción propiedad de IVANNA, no el audio global de aplicaciones ajenas.
- Se reutilizan los buffers de trabajo L/R y coeficientes entre bloques para reducir asignaciones por chunk. Las muestras NaN/Inf se sustituyen por cero antes de entrar al FIR; la ruta transparente a razón unidad solo se conserva cuando todas las muestras son finitas.
- Se añadieron pruebas JVM para la entrada no finita tanto en conversión como en razón unidad. **Pendiente:** CI del commit actual y medición física en Moto G85.
- Existe por separado `UsbIngressAsrc` nativo para la ruta USB. No se añadió un segundo motor C++ duplicado a la reproducción de la app.
- **HAL/PAL de proveedor:** sigue sin demostrarse que el ASRC software de la app controle el actuador del HAL Motorola/Qualcomm. No se declara corrección del reloj físico del DAC ni procesamiento global hasta verificar el callback/actuador real y las mediciones del dispositivo.


## ASRC software — endurecimiento de reproducción propia (2026-10-22, no publicado)

- El cambio de rendimiento reutiliza los espacios de trabajo L/R y los coeficientes FIR entre bloques; el buffer de salida usa una cota calculada con el retardo causal del filtro para evitar la asignación y copia de dos muestras extra por bloque.
- El ASRC sanitiza NaN/Inf antes de incorporarlos al historial FIR. En razón unidad solo conserva la vía bit-transparente si todas las muestras son finitas.
- Pruebas JVM nuevas: entrada no finita en remuestreo y en razón unidad, salida finita y protección del historial del siguiente bloque.
- **No se declara CI verde**: el conector disponible no devolvió checks/workflow de la rama principal para el commit documental consultado. Hay que confirmar el resultado en GitHub Actions del SHA más reciente.
- **No se declara validación física**: falta medir latencia, respuesta de frecuencia/alias, underruns y consumo en el Moto G85. El ASRC app-owned sigue limitado al reproductor de IVANNA; no prueba control del reloj físico ni de apps de terceros.
