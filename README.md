<div align="center">

<img src="docs/release_media/ivanna_omega_hero.svg" alt="IVANNA OMEGA SUPREME — audio digital y herramientas espaciales para Android" width="100%" />

# IVANNA OMEGA SUPREME

### Audio digital · herramientas espaciales · medición y verificación

<p>
  <a href="LEAME.md">Guía en español sencillo</a> ·
  <a href="docs/ENGINEERING.md">Ficha de ingeniería</a> ·
  <a href="CHANGELOG.md">Registro de cambios</a> ·
  <a href="docs/PLAN_10D_MOTO_G85_ACCEPTANCE.md">Aceptación Moto G85</a>
</p>

[![CI](https://img.shields.io/github/actions/workflow/status/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/build.yml?branch=main&label=CI%20main)](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions/workflows/build.yml)
[![Android](https://img.shields.io/badge/plataforma-Android-3DDC84?logo=android&logoColor=white)](app/)
[![Kotlin](https://img.shields.io/badge/app-Kotlin%20%2F%20Compose-7F52FF?logo=kotlin&logoColor=white)](app/src/main/)
[![C++](https://img.shields.io/badge/motor-nativo%20C%2B%2B-00599C?logo=cplusplus&logoColor=white)](app/src/main/cpp/)
[![Versión](https://img.shields.io/badge/versión-2.5.126-64748b)](version.properties)

**Un proyecto de ingeniería en desarrollo.** Documentamos lo que existe en el código, qué evidencia hay y qué todavía requiere validación real.

</div>

---

## En una mirada

IVANNA OMEGA SUPREME combina una aplicación Android en Kotlin, componentes nativos de procesamiento de audio en C++, herramientas de medición acústica y módulos de visualización espacial. El propósito es construir rutas de procesamiento y calibración que se puedan inspeccionar y probar, sin convertir una intención de diseño en una promesa de resultado.

<table>
<tr>
<td width="33%" valign="top">

### 🎚️ Audio

Reproducción y procesamiento desde la aplicación, resampling y un control software de cola de AudioTrack en la ruta propia del reproductor.

</td>
<td width="33%" valign="top">

### 📐 Medición

Herramientas para reproducir tonos de prueba, registrar la respuesta del micrófono y comparar una medición antes y después de un ajuste.

</td>
<td width="33%" valign="top">

### 🧭 Espacio

Renderizador geométrico y una ruta de captura AR experimental que puede recopilar puntos espaciales y exportar datos.

</td>
</tr>
</table>

> **Regla de lectura:** que un módulo, modelo, dataset, control o contrato esté presente en el repositorio no demuestra por sí solo que esté conectado al audio activo, que funcione en todos los dispositivos ni que haya sido validado físicamente.

## Mapa visual de la arquitectura

<img src="docs/release_media/architecture-map.svg" alt="Diagrama de arquitectura de IVANNA OMEGA SUPREME, sus módulos Android, audio, medición, AR y la integración HAL/PAL pendiente de validar" width="100%" />

El diagrama representa las relaciones generales del código. En particular, el bloque HAL/PAL es un punto de integración documentado: no es evidencia de que exista control físico del reloj o del ASRC del proveedor en el Moto G85.

## Componentes y estado real

<table>
<thead><tr><th>Área</th><th>Qué contiene el proyecto</th><th>Estado y límites</th></tr></thead>
<tbody>
<tr>
<td><strong>Aplicación Android</strong></td>
<td>Interfaz Kotlin/Jetpack Compose, servicios y puentes hacia componentes nativos.</td>
<td>Código presente; la funcionalidad concreta debe evaluarse por ruta y por versión.</td>
</tr>
<tr>
<td><strong>Motor DSP nativo</strong></td>
<td>Código C++ y enlace JNI para las funciones nativas que llaman desde la app.</td>
<td>La existencia del motor no prueba que cada etapa experimental participe en toda reproducción.</td>
</tr>
<tr>
<td><strong>PLL software de AudioTrack</strong></td>
<td><code>AudioTrackBufferPll</code> estima la ocupación de la cola del reproductor de la app y ajusta el resampler dentro de límites de software.</td>
<td>Implementación a nivel de código. No controla el reloj físico del DAC, Bluetooth ni el HAL global; necesita pruebas de reproducción reales para evaluar estabilidad, clics, latencia y underruns.</td>
</tr>
<tr>
<td><strong>Cadena de reproducción del reproductor propio</strong></td>
<td><code>IvannaBridgePlayer</code> decodifica, procesa y escribe en <code>AudioTrack</code>. Si el DSP nativo (<code>DSPBridge</code>) está cargado, ejecuta la cadena maestra y la ruta Kotlin no apila procesadores duplicados; si no, se usan NPE, Volterra, modo concierto y motor espacial según sus interruptores.</td>
<td>Aplica solo al audio que reproduce IVANNA. No procesa el audio de otras apps ni de todo el sistema por sí mismo. Qué etapas se oyen y con qué efecto debe comprobarse con escucha y registros en el Moto G85.</td>
</tr>
<tr>
<td><strong>Motor predictivo (Módulo 1)</strong></td>
<td><code>predictive_audio_engine</code> analiza el audio en C++ y publica confianza, ganancia estimada y transitorios por JNI hacia la interfaz y las métricas.</td>
<td>Es un <strong>observador</strong>: el reproductor le entrega el bloque ya procesado y no lo modifica. Hoy produce telemetría, no cambia la señal de salida. La ventana de predicción de 5–10 ms es un objetivo de diseño sin medición en dispositivo registrada.</td>
</tr>
<tr>
<td><strong>HRTF / SOFA / SAF</strong></td>
<td>Cargadores y convolucionador HRTF, bases PCA y paquetes de datos. La puerta <code>check_datasets_deploy.py</code> verifica que los 239 <code>.sofa</code>, 12 HRTF y 200 RIR del APK coincidan con los del módulo Magisk y que los archivos HDF5 estén íntegros.</td>
<td>La integridad y la coherencia de los archivos están comprobadas de forma automática. Que la espacialización binaural resulte audible, correcta y cómoda en el teléfono no está demostrado por esa puerta y requiere escucha controlada. Una HRTF genérica no equivale a la HRTF personal de cada oyente.</td>
</tr>
<tr>
<td><strong>Anti-Dolby / clasificador de contenido</strong></td>
<td>Modelo <code>anti_dolby_crnn.tflite</code> que clasifica el contenido en voz, música, bajos y silencio, y lógica nativa que usa esa clasificación.</td>
<td>Es un clasificador de contenido, no un modelo entrenado de política acústica universal. Las decisiones de orquestación son deterministas y acotadas. No se afirma un desprocesamiento verificado frente a ningún códec concreto sin mediciones con material de referencia.</td>
</tr>
<tr>
<td><strong>Medición y corrección de sala</strong></td>
<td>Secuencia de tonos, captura de micrófono, comparación antes/después y lógica de restauración cuando el resultado no cumple el criterio interno.</td>
<td>El micrófono del teléfono no es un instrumento calibrado. La métrica interna no equivale a una mejora audible universal ni a una certificación acústica.</td>
</tr>
<tr>
<td><strong>FotoHolo / captura AR</strong></td>
<td>Actividad de captura con ARCore, seguimiento espacial, puntos 3D y flujo de exportación.</td>
<td>Experimental. No debe confundirse una nube de puntos con una malla completa, un plano certificado ni una medición RT60/RIR. Requiere probar permisos, seguimiento, pausa/reanudación y exportación en el equipo.</td>
</tr>
<tr>
<td><strong>Renderizador geométrico</strong></td>
<td><code>SceneGeometryRenderer</code> genera imágenes a partir de una configuración geométrica; el código contempla hasta 3840 × 2160 píxeles.</td>
<td>Renderiza la geometría configurada. No es por sí solo una reconstrucción automática de la habitación ni una simulación acústica validada.</td>
</tr>
<tr>
<td><strong>SAFMUS / ACSR / HAL-PAL</strong></td>
<td>Hay lógica, adaptadores o contratos de integración en distintas partes del repositorio.</td>
<td>La documentación de integración no demuestra una unión de extremo a extremo con el hardware del fabricante. No afirmamos un actuador PAL/ASRC operativo ni control físico de reloj en el Moto G85 sin lectura de retorno y pruebas.</td>
</tr>
<tr>
<td><strong>Wi-Fi / CSI</strong></td>
<td>Políticas conservadoras para tratar observaciones de radio como indicios de contexto.</td>
<td>Las API Wi-Fi comunes no equivalen a CSI. La disponibilidad depende de hardware y proveedor compatibles; no se afirma reconstrucción anatómica ni compatibilidad universal.</td>
</tr>
</tbody>
</table>

## Flujos que importan

### Medición: medir, decidir y verificar

```mermaid
flowchart LR
    A["Medición inicial"] --> B["Diseñar corrección"]
    B --> C["Aplicar ajuste"]
    C --> D["Medición posterior"]
    D --> E{"¿Datos válidos y criterio cumplido?"}
    E -->|Sí| F["Aceptar y registrar"]
    E -->|No| G["Restaurar estado anterior"]
    G --> H["Registrar motivo"]
```

Este flujo describe la lógica implementada en la ruta de medición. La fiabilidad depende de la señal, el ruido, la posición, el volumen, la salida y las características del micrófono. La reversión de un ajuste no sustituye las pruebas de seguridad de todas las rutas de audio.

### Reproducción: alcance del PLL software

```mermaid
flowchart TD
    A["Audio de la app"] --> B["AudioTrack"]
    B --> C["Timestamp / estimación de cola"]
    C --> D["AudioTrackBufferPll"]
    D --> E["Corrección limitada en ppm"]
    E --> F["StereoAudioResampler"]
    F --> B
    H["Datos inválidos o desalineados"] -.-> I["Reset a corrección neutra"]
    I -.-> D
```

El PLL trabaja sobre el resampler del reproductor propio de IVANNA. No puede inferir ni escribir automáticamente el reloj físico de un DAC, una ruta Bluetooth o un módulo HAL. El funcionamiento auditivo y la estabilidad deben comprobarse en el teléfono con cada salida que se quiera declarar compatible.

## FotoHolo y visualización 3D

- **Captura AR:** usa las capacidades que ARCore y el dispositivo expongan; la disponibilidad de profundidad no es universal.
- **Nube de puntos:** es una representación parcial de las observaciones de captura, no una reconstrucción cerrada de toda la habitación.
- **Renderizado 3D:** la imagen se deriva de la geometría configurada en el módulo; la resolución de salida no garantiza realismo fotorrealista.
- **Acústica:** la geometría visual no es una medición de respuesta al impulso, reverberación ni absorción de materiales.

## Privacidad y claves de API

El procesamiento local que no requiere servicios externos debe poder operar sin una clave de Google. Una clave solo es necesaria para una función opcional cuando esa función invoque realmente un servicio externo que la exija. No se debe interpretar la presencia de una interfaz o configuración de proveedor como prueba de que la integración remota esté activa.

La cámara y el micrófono deben solicitarse cuando una función los necesita. Antes de compartir diagnósticos, elimina credenciales, tokens, datos personales y grabaciones privadas.

## Compilar y ejecutar comprobaciones

### Requisitos

Consulta las versiones exactas configuradas por el proyecto y por CI. La configuración actual utiliza Android SDK, NDK, CMake, Gradle Wrapper y Node.js para los componentes correspondientes. La reproducción completa de CI puede requerir Linux y dependencias adicionales.

### Pruebas nativas en host

Desde la raíz del repositorio:

```bash
cmake -B build-tests -S app/src/main/cpp/tests -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests -j4
ctest --test-dir build-tests --output-on-failure
```

### Puertas estáticas

```bash
for s in scripts/check_*.py; do
  python3 "$s" || exit 1
done
```

Estos comandos no sustituyen la compilación Android, las pruebas instrumentadas ni la validación del teléfono. Para reproducir exactamente la entrega, revisa [el flujo de GitHub Actions](.github/workflows/build.yml).

## Estado de CI, versiones y pruebas físicas

- [Ver ejecuciones de CI](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions)
- [Ver versiones publicadas](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/releases)
- [Ver código de la versión indicada](version.properties)
- [Plan de aceptación para Moto G85 / Android 15](docs/PLAN_10D_MOTO_G85_ACCEPTANCE.md)

El distintivo CI refleja el estado del flujo configurado en GitHub; no es una certificación acústica ni demuestra que se haya probado el teléfono. Antes de considerar una versión lista, verifica que la compilación, los tests, el APK y el ZIP Magisk procedan del mismo commit, inspecciona sus hashes y prueba la instalación en el dispositivo de destino.

### Lista de aceptación en hardware

- [ ] Instalar APK y módulo generados desde el mismo commit aprobado.
- [ ] Probar la salida integrada, USB-C DAC/adaptador y Bluetooth por separado cuando estén disponibles.
- [ ] Confirmar que el reproductor propio avanza, que la corrección permanece dentro de sus límites y que no aparecen clics, cambios de tono o underruns anómalos.
- [ ] Abrir FotoHolo, confirmar cámara en vivo, seguimiento, pausa/reanudación y exportación.
- [ ] Registrar el modelo, versión Android, ruta, SHA del commit, hashes de artefactos y resultados.
- [ ] No declarar HAL/PAL conectado sin identificar el callback real, unidades del parámetro, actuador, lectura de retorno y prueba en hardware.

## Contribuir y reportar problemas

Los informes más útiles incluyen el commit o versión probados, modelo de dispositivo, versión de Android, ruta de audio, pasos para reproducir y registros depurados de datos personales.

- [Abrir un issue](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/issues)
- [Revisar cambios](CHANGELOG.md)
- [Consultar documentación de ingeniería](docs/ENGINEERING.md)

## Licencia y propiedad intelectual

IVANNA OMEGA SUPREME se presenta como un producto propietario. La visibilidad pública del repositorio no concede por sí sola permiso para reutilizar el código propio. Los componentes, bibliotecas, modelos y datasets de terceros conservan sus licencias y avisos; revisa los archivos de licencia y avisos antes de redistribuir. Las concesiones expresas aplicables a versiones anteriores siguen rigiendo esas versiones según sus términos.

---

<div align="center">

**Construir con rigor. Medir cuando sea posible. Declarar solo lo que la evidencia respalda.**

<sub>La arquitectura y las funciones evolucionan; el código, las pruebas reproducibles y la validación física tienen prioridad sobre cualquier etiqueta de marketing.</sub>

</div>
