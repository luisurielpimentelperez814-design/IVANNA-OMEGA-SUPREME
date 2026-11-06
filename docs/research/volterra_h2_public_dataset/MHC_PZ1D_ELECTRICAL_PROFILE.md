# Perfil eléctrico documentado — Sony MHC-PZ1D / altavoces suministrados

**Fuente:** manual de usuario Sony MHC-PZ1D, sección de especificaciones (páginas impresas 97ES–99ES). Este perfil conserva por separado los valores del amplificador y las capacidades nominales de los altavoces. No representa una medición acústica del equipo.

## Datos transcritos del manual

| Elemento | Impedancia nominal | Potencia indicada |
|---|---:|---:|
| Amplificador, LINK MODE | 6 Ω | 135 W + 135 W, 1 kHz, 1 % THD |
| Altavoces delanteros SS-GZR999D | 8 Ω | 210 W + 210 W RMS (otros modelos) |
| Satélites SS-RSZ999D | 24 Ω | 90 W + 90 W RMS (otros modelos) |
| Subwoofer SS-WGR999D | 6 Ω | 240 W RMS (otros modelos) |
| Entrada VIDEO/SAT AUDIO IN L/R | 47 kΩ | 250/450 mV |
| Alimentación del modelo México | — | 127 V, 60 Hz |

Las potencias RMS de los altavoces son especificaciones de referencia/capacidad de los sistemas de altavoces; **no** deben interpretarse como potencia simultánea entregada por cada canal del amplificador. La potencia nominal del amplificador es una especificación distinta.

## Cálculos eléctricos equivalentes

Para contextualizar cada combinación nominal se calcula, únicamente como equivalencia matemática, la tensión y corriente RMS que corresponderían a P sobre una carga resistiva R:

- (V_{RMS}=\sqrt{P R})
- (I_{RMS}=\sqrt{P/R}=V_{RMS}/R)

| Referencia del manual | P | R | V RMS equivalente | I RMS equivalente |
|---|---:|---:|---:|---:|
| Amplificador LINK MODE (por canal) | 135 W | 6 Ω | 28.46 V | 4.74 A |
| Delantero (cada sistema) | 210 W | 8 Ω | 40.99 V | 5.12 A |
| Satélite (cada sistema) | 90 W | 24 Ω | 46.48 V | 1.94 A |
| Subwoofer | 240 W | 6 Ω | 37.95 V | 6.32 A |

**Límite de interpretación:** los tres últimos renglones no son tensiones de salida medidas ni objetivos de excitación. Son conversiones de las cifras nominales de potencia/impedancia del manual. No conectar una salida de altavoz directamente a una entrada de línea o a un teléfono; cualquier captura eléctrica futura requiere atenuación/aislamiento adecuados.

## Qué parámetros sí pueden fijarse desde el manual

- Identidad documental del conjunto: MHC-PZ1D con SS-GZR999D, SS-RSZ999D y SS-WGR999D.
- Impedancias nominales por tipo de altavoz.
- Sensibilidad nominal de entrada auxiliar: 250/450 mV sobre 47 kΩ.
- Alimentación regional México: 127 V, 60 Hz.
- Referencia de potencia nominal del amplificador y capacidades RMS de los altavoces, manteniéndolas como campos distintos.

## Qué NO se puede calcular desde el manual

No hay respuesta impulsional acústica medida del conjunto instalado, curva compleja de transferencia, fase armónica, excitación/salida sincronizadas, serie de niveles ni registro de dos tonos. Por tanto, no se pueden derivar coeficientes H2 de Volterra, distorsión armónica real, compresión, parámetros de suspensión/aire ni una ecualización inversa segura a partir de estas especificaciones.

El kernel H2 permanece en bypass hasta disponer de datos de entrada/salida identificables. El perfil documental no activa H2 ni autoriza elevar la ganancia. La corrección de orden triangular de H2 incluida en esta rama solo asegura que las herramientas Python empaqueten los coeficientes en el mismo orden que el consumidor C++.
