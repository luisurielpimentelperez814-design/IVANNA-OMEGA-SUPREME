# Volterra H2 — parámetros calculables y puerta de activación

## 1. Estado real de la ruta de audio

El repositorio ya contiene una llamada a `VolterraH2Symmetric::processInterleaved()` dentro de `omega_effect.cpp`, después de preparar los buffers estéreo. Sin embargo, esa ruta solo reclama el slot de procesamiento si `hasEffectiveKernel()` es verdadero. El constructor crea H1 identidad y no reserva H2; por tanto, el objeto existe y la ruta está cableada en el efecto, pero no ejecuta una corrección H2 útil mientras ningún consumidor de producción cargue coeficientes mediante `updateKernels()`.

Esto es una ruta del efecto IVANNA/AudioEffect, no prueba de integración en el HAL propietario `audio.primary.blair.so` ni en la ruta PAL global del sistema. La activación en una app/efecto y la activación global de Android son entregables distintos.

## 2. Parámetros que sí se pueden calcular

### 2.1 Tamaño del motor actual

El motor se instancia con K=64 taps y 2 canales.

- H1: K = 64 coeficientes por canal.
- H2 simétrico: K(K+1)/2 = 64×65/2 = 2,080 coeficientes por canal.
- Parámetros por canal: 64 + 2,080 = 2,144.
- Mínimo de muestras impuesto por el estimador actual: 4×2,144 = 8,576 muestras por registro. Esto es un umbral de tamaño, no garantía de identificabilidad.
- Memoria de coeficientes float32: (64 + 2,080)×4 = 8,576 bytes por canal; 17,152 bytes para dos canales si se almacenan kernels independientes. La implementación actual comparte una instancia/coeficientes entre canales y mantiene historial por canal; no debe confundirse con calibración L/R independiente.

### 2.2 Especificación de entrada AUX documentada

El manual MHC-PZ1D indica 250/450 mV y 47 kΩ para VIDEO/SAT AUDIO IN L/R. Interpretando los voltajes como RMS sobre esa impedancia:

| Nivel AUX | R | Corriente RMS V/R | Potencia RMS V²/R |
|---:|---:|---:|---:|
| 0.250 V | 47,000 Ω | 5.319 µA | 1.330 µW |
| 0.450 V | 47,000 Ω | 9.574 µA | 4.309 µW |

Son equivalencias de la especificación de entrada, no una medición del DAC USB-C, ni tensión/corriente de altavoz. No permiten calcular THD, fase, compresión o H2.

### 2.3 Potencia e impedancia nominales del manual

Usando V_RMS=√(P·R) e I_RMS=√(P/R):

| Especificación | P | R | V RMS equivalente | I RMS equivalente |
|---|---:|---:|---:|---:|
| Amplificador LINK MODE, por canal | 135 W | 6 Ω | 28.46 V | 4.74 A |
| Delantero (capacidad indicada) | 210 W | 8 Ω | 40.99 V | 5.12 A |
| Satélite (capacidad indicada) | 90 W | 24 Ω | 46.48 V | 1.94 A |
| Subwoofer (capacidad indicada) | 240 W | 6 Ω | 37.95 V | 6.32 A |

Las potencias de altavoz son capacidades nominales descritas en la sección de especificaciones, no potencia entregada por cada canal del amplificador. Estos valores no deben usarse como objetivos de excitación ni para ajustar ganancia digital.

## 3. Corrección matemática requerida en H2

El C++ consume el triángulo superior en orden por filas:
`i(k,l)=kK-k(k+1)/2+l`, para `0≤k≤l<K`.

La suma de una matriz simétrica completa contiene dos términos para cada par fuera de la diagonal:
`h2[k,l]x[n-k]x[n-l] + h2[l,k]x[n-l]x[n-k]`.
Por ello, el C++ multiplica por 2 los términos fuera de la diagonal y por 1 la diagonal. El identificador Python debe usar el mismo factor en el modelo sintético y en cada columna de la matriz de diseño LS. La rama incluye la corrección de orden y de escala, además de pruebas de paridad.

## 4. Datos indispensables para derivar un kernel H2 físico

Los siguientes campos no están en el manual ni en el dataset público:

1. Identidad inequívoca de la cadena: modelo exacto del amplificador/transductores, topología y si el PDF MHC-PZ1D corresponde realmente al equipo llamado Genezy MHCPZ1. No mezclar ambos nombres sin confirmar.
2. DAC USB-C: fabricante/modelo, USB VID/PID, modo de salida, fs real y tensión RMS máxima a 0 dBFS; volumen del sistema y volumen físico fijados durante toda la sesión.
3. Captura sincronizada: WAV PCM de la excitación digital exacta y WAV/captura cruda de salida, con reloj común o canal de referencia. Debe conocerse la latencia y no puede haber AGC, limitador o procesamiento automático desconocido entre ambos.
4. Tres niveles de excitación como mínimo (el protocolo propone −20, −12 y −6 dBFS), y registros de validación separados de los usados para estimar.
5. Barrido/sweep con fase conocida para H1 y H2; tonos individuales con magnitud y fase armónica; pares de dos tonos para intermodulación. Las curvas de THD agregadas no sustituyen a las muestras crudas sincronizadas.
6. L/R capturados simultáneamente o por separado con una referencia estable; frecuencia de muestreo real, canales, formato, temperatura y SHA-256 de cada archivo.
7. Para separar DAC/amplificador de transductor/aire: medición eléctrica diferencial en la salida AUX/entrada y/o en una terminación de prueba apropiada, más la captura acústica. Una única medición acústica solo identifica la cadena completa, no cada componente por separado.

El dataset público disponible no tiene excitación cruda, salida cruda, fase armónica ni dos tonos; por eso no identifica H2 del sistema objetivo. El manual solo da impedancias/potencias/entrada, no coeficientes no lineales.

## 5. Puerta de activación en producción

Antes de activar:
- corregir y probar el empaquetado triangular/escala simétrica;
- parser nativo C++/JNI para el paquete IVK1, validando CRC, procedencia real/hash, dispositivo, K, fs, finitud, límites y estabilidad;
- integrar la carga desde hilo de control (nunca desde callback RT): deshabilitar motor, esperar DSP silencioso y cero callbacks activos, llamar `updateKernels()`, verificar `hasEffectiveKernel()`, luego reactivar;
- sustituir la frecuencia fija de 48 kHz del envelope por la frecuencia real validada de la sesión, o rechazar cualquier kernel con fs distinto;
- telemetría de kernel efectivo, conteo de bloques procesados, picos, clipping y bypass; A/B con rollback;
- ejecutar pruebas C++/Python, CI Android ARM64 y prueba en Moto G85 antes de habilitar por defecto.

**Política de activación:** H1 medido puede prepararse como etapa lineal independiente si la cadena y fs coinciden y pasa las pruebas. H2 no debe activarse con coeficientes estimados solo desde las potencias nominales. Hasta que existan los registros identificables, conservar H2 en bypass es el único estado técnicamente defendible. El cableado de software ya existente no equivale a una calibración ni a una activación funcional.
