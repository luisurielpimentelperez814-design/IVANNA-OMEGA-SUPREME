# Validación en dispositivo con Termux

Este procedimiento es **solo lectura**: no requiere root, no cambia permisos, no modifica perfiles de audio y no escribe en particiones del sistema.

## 1. Preparación

En Termux:

```bash
pkg update
pkg install coreutils grep
```

Copia `scripts/termux_validate_ivanna.sh` al teléfono y ejecútalo:

```bash
chmod +x termux_validate_ivanna.sh
./termux_validate_ivanna.sh
```

Si conoces el identificador real del paquete, pásalo como argumento:

```bash
./termux_validate_ivanna.sh com.ivanna.omega
```

El nombre de ejemplo anterior debe confirmarse con el `applicationId` del APK; no se debe asumir si el paquete instalado usa otro identificador.

## 2. Qué demuestra y qué no

- **Paquete/versión:** confirma que Android ve el paquete y muestra sus metadatos cuando el contexto de Termux permite consultarlos.
- **Ruta Android:** captura una vista parcial de los dispositivos y la política de audio visibles para el usuario.
- **Logcat:** busca eventos de los componentes de IVANNA que hayan emitido logs. Un log ausente no prueba que el componente no exista; puede no tener permisos, no estar activo o no registrar ese evento.
- **No certifica el DSP:** los datos anteriores no demuestran por sí solos que ASRC, NHO/transitorios, HRTF/SOFA, RIR, WFS/FotoHolo y SAF hayan procesado el mismo bloque de audio.

Si Android deniega `pm`, `dumpsys` o `logcat` desde Termux, usa ADB desde un equipo autorizado; no intentes eludir controles de seguridad ni cambiar SELinux/vendor mixer para obtener una lectura.

## 3. Prueba de integración requerida

1. Ejecutar las pruebas host del repositorio (`bash scripts/run_ctest.sh`) en un entorno Linux/CI con CMake y compilador disponibles.
2. Instalar la APK de una compilación identificada por commit y registrar `versionName`, `versionCode` y SHA-256 del archivo APK.
3. Reproducir una señal de prueba con bypass y con procesamiento activado, igualando nivel de salida.
4. Registrar la ruta de salida usada en cada ensayo: altavoz, Bluetooth o USB-C/DAC.
5. Guardar los logs y reportar cortes, clipping, latencia y resultados de escucha. Si no hay instrumentación de etapas, etiquetar la etapa como **no confirmada**, no como activa.

## Seguridad

El script no inicia reproducción, no cambia volúmenes, no activa motores, no concede permisos y no intenta modificar el audio de otras aplicaciones. Los resultados de logs pueden contener metadatos del dispositivo; revísalos antes de compartirlos.
