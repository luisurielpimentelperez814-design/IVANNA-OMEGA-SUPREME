# IVANNA Genezi Spatial — modelo y validación

Estado: **beta de procesamiento estéreo**, no calibración acústica del Sony.

## Objetivo

Preparar la señal AUX L/R para el upmix interno del Sony Genezi MHC-PZ1D con una anchura diferencial conservadora. El perfil no genera cinco canales discretos, no controla el botón Surround ni el subwoofer y no presume conocer la respuesta electroacústica real del equipo.

## Modelo

Sea la entrada (x=[L,R]^T). La descomposición Mid/Side es:

```text
M = (L + R) / 2
S = (L - R) / 2
Lout = M + g(f) S
Rout = M - g(f) S
```

El caso `g=1` es la señal estéreo original. `g>1` aumenta la diferencia lateral; no garantiza mejor percepción y puede acentuar cancelaciones al sumar a mono. El widener nativo de IVANNA separa Side de baja y alta frecuencia y limita el realce de Side en graves al ensanchar. El perfil AUX beta solicita una anchura conservadora de 1.08; no es un valor medido del Sony ni una afirmación de ganancia acústica.

## Propiedades y límites

- Centro algebraicamente preservado en el modelo M/S ideal.
- Sin boost de graves ni boost de diálogo en AUX.
- El ensanchador existente suaviza el cambio de anchura y limita el Side de baja frecuencia al ensanchar.
- La suma mono elimina el Side añadido en el modelo ideal; el contenido original puede tener sus propias cancelaciones.
- El perfil es una precondición estéreo para el upmix del Genezi, no salida multicanal discreta ni simulación binaural de altavoces traseros.
- No se han medido en la habitación del usuario respuesta impulsional, transferencia por modo, crossover, ganancia de satélites ni interacción Surround/subwoofer. No se afirma una mejora acústica verificada.

## Protocolo de validación

1. Mantener el mismo archivo, posición, nivel de volumen y ecualización en los cuatro estados Stereo/Sub OFF, Stereo/Sub ON, Surround/Sub OFF y Surround/Sub ON.
2. Comparar el perfil AUX neutro y Genezi Spatial con loudness igualado; registrar picos, RMS/LUFS, correlación L/R y señal mono sumada.
3. Comprobar voces centradas, estabilidad de graves, transitorios y ausencia de clipping o bombeo.
4. Realizar prueba ABX aleatorizada si se pretende afirmar preferencia perceptual.
5. Para cuantificar la respuesta del amplificador, medir cada altavoz por separado con micrófono calibrado y conservar la geometría; la medición acústica incluye la sala. No conectar nunca una salida amplificada de altavoz directamente al teléfono.

## Ecuaciones de evaluación

```text
rho = sum(L*R) / sqrt(sum(L^2)*sum(R^2) + eps)
mono = (Lout + Rout) / 2
peak = max(max(abs(Lout)), max(abs(Rout)))
interaction(f) = [D(f)-C(f)] - [B(f)-A(f)]
```

La correlación no es una puntuación de calidad por sí sola. Las diferencias de nivel entre estados deben calcularse sobre capturas alineadas y con una referencia de nivel común. La interacción D-C/B-A debe expresarse en dB únicamente tras medir respuestas válidas y evitar pisos de ruido.

## Criterio de aceptación

El perfil solo se promociona de beta a calibrado cuando el build y las pruebas automáticas pasan y las comparaciones medidas no muestran clipping, degradación de mono ni pérdida central relevante. CI valida el código; no sustituye las mediciones físicas del Genezi.
