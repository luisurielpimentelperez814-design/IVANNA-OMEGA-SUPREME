# Análisis de referencia Sony MHC-PZ1D / MHC-GZR999D para Volterra H1/H2

Fecha de revisión: 2026-11-06.

## Hallazgo principal

La referencia pública más cercana localizada no es un equipo genérico: Sony HCD-GZR999D comparte con HCD-PZ1D el híbrido de potencia STK416-130-E. El manual HCD-GZR999D identifica ese IC en el esquema POWER AMP (pág. impresa 63); el manual HCD-PZ1D identifica el mismo IC en el esquema AMP (pág. impresa 64). El MHC-PZ1D documenta el conjunto de altavoces SS-GZR999D, SS-RSZ999D y SS-WGR999D. El manual GZR999D documenta las mismas clases de salida nominal y el mismo amplificador de 135 W + 135 W a 6 Ω, 1 kHz, 1 % THD en LINK MODE.

Fuentes:
- Sony HCD-PZ1D Service Manual: https://www.manualslib.com/manual/2927728/Sony-Hcd-Pz1d.html
- Sony HCD-GZR999D Service Manual: https://www.manualslib.com/manual/3886066/Sony-Hcd-Gzr999d.html
- Sony Latin America MHC-PZ1D: https://www.sony-latin.com/es/electronics/support/audio-systems-mhc-series/mhc-pz1d
- Datasheet Sanyo STK416-130-E: https://agelectronica.lat/pdfs/textos/S/STK%20416-130.PDF

## Parámetros transferibles como restricciones del perfil

| Parámetro | Valor documental |
|---|---:|
| Amplificador MHC-PZ1D / MHC-GZR999D, LINK MODE | 135 W + 135 W, 6 Ω, 1 kHz, 1 % THD |
| IC de potencia | STK416-130-E, híbrido clase H de 3 canales |
| Frontales SS-GZR999D | 8 Ω nominales; 210 W + 210 W RMS de referencia |
| Satélites SS-RSZ999D | 24 Ω nominales; 90 W + 90 W RMS de referencia |
| Subwoofer SS-WGR999D | 6 Ω nominales; 240 W RMS de referencia; cifra de potencia indicada a 100 Hz |
| Entrada VIDEO/SAT AUDIO IN | 250/450 mV sobre 47 kΩ |
| Alimentación mexicana | 127 V, 60 Hz |

Las potencias de altavoces son valores de referencia/capacidad y no significan que el amplificador entregue esas potencias simultáneamente a todos los canales.

## Qué aporta el datasheet del STK416-130-E

El datasheet del híbrido especifica, en condiciones de laboratorio y carga resistiva no inductiva de 8 Ω, 100 W, alimentación VH=±51 V / VL=±34 V, THD típica de 0.4 % para 20 Hz–20 kHz. También indica respuesta de frecuencia 20 Hz–50 kHz (+0/−3 dB) bajo sus condiciones de ensayo. Describe un amplificador clase H con conmutación de alimentación dependiente del nivel de señal.

Estos datos son límites/referencias del IC aislado, no una medición del HCD-PZ1D completo ni de los transductores en caja. No contienen fase compleja por armónico ni resultados de dos tonos.

## Dictamen para H1

El H1 del equipo objetivo debe proceder de una respuesta impulsional del propio equipo objetivo, capturada con referencia de nivel, reloj, canal, ruta, volumen, controles de tono/DSP y geometría documentados. El REW .mdat público inventariado en este repositorio se identifica como `kuulavaimennintesti202050914_nimetty.mdat`, dentro del conjunto `optimized_soft_enclosure_particle_dampers_rew_dataset_v1.zip`; no debe etiquetarse automáticamente como H1 del Sony ni del equipo Genezy. Además, una IR acústica medida con micrófono contiene sala, posición y micrófono; no equivale por sí sola a la transferencia aislada del amplificador.

El motor consume H1 como FIR de corrección. Por tanto, una IR de transferencia directa no debe copiarse sin más a `h1_kernel`: primero debe definirse si el modelo es directo o inverso y, para corrección, calcular una inversa regularizada con límites de ganancia, latencia y fase mínima/mixta explícitos.

## Dictamen para H2

Ni el manual de servicio Sony ni el datasheet del STK aportan:
- amplitud y fase compleja de H2 por frecuencia;
- fase de segundo y tercer armónico frente a nivel;
- registros de entrada/salida sincronizados;
- ensayos de dos tonos con productos de suma y diferencia;
- barrido de varios niveles para separar no linealidad de compresión/limitación;
- comportamiento térmico y de conmutación de alimentación en el conjunto real.

Por ello, no es matemáticamente válido fabricar `h2[k,l]` a partir de 135 W, 1 % THD o 0.4 % THD. Un escalar THD no determina el orden, fase ni memoria del término no lineal. El modo clase H añade dependencia de nivel/estado de alimentación, que refuerza la necesidad de identificación por niveles y señales de dos tonos.

## Política de perfil provisional

1. El Sony MHC-GZR999D es el análogo documental más fuerte localizado para el MHC-PZ1D por el mismo STK416-130-E y especificaciones nominales concordantes.
2. Sus datos sirven para etiquetar restricciones y construir un perfil de referencia; no sirven para poblar H2 ni para afirmar equivalencia exacta de preamplificador, filtros, fuente, PCB o cajas.
3. No generar ni cargar coeficientes H2 Sony como si fueran mediciones del equipo objetivo.
4. Mantener H2 fail-closed/bypass hasta que exista una identificación compleja y de dos tonos del conjunto exacto.
5. El cargador IVK1 de producción debe rechazar kernels con identidad de equipo/dataset no autorizada, sample rate distinto, CRC inválido, coeficientes no finitos o límites de estabilidad excedidos.


## Perfil experimental medido aportado para Sony HCD-PZ1D / MHC-PZ1D

**Procedencia:** conjunto de datos de laboratorio aportado por el usuario en la conversación del 2026-11-06. Para este perfil se incorporan como mediciones del usuario; no se atribuyen al datasheet de Sanyo ni al manual Sony. Mantener la trazabilidad de esta procedencia al exportar o entrenar kernels.

### Rieles y umbral Clase H

| Variable | Valor incorporado | Condición / interpretación |
|---|---:|---|
| VH positivo / negativo | ±58.2 V DC | Nominal operativo reportado |
| VH en vacío | ±62.1 V DC | Reportado |
| VH a máxima potencia, 6 Ω | ±52.4 V DC | Reportado |
| VL positivo / negativo | ±31.5 V DC | Reportado como estabilizado |
| Umbral de conmutación | ±24.5 V pico | Medido en nodos de salida, pines 11/12/13 según el usuario |
| Margen de guarda | 7.0 V | 31.5 − 24.5 V |
| Tiempo de encendido | 850 ns | Reportado |
| Retención / apagado | 4.5–12 ms | Reportado; se conserva como intervalo, no como constante única |

El umbral de 24.5 V pico sobre una carga resistiva de 6 Ω equivale a 50.0 W sinusoidales: P = Vpk²/(2R). Esta equivalencia es una comprobación matemática.

### Armónicos reportados

| Condición | Componente | Nivel | Relación de amplitud |
|---|---|---:|---:|
| 1 kHz, 6 Ω, 50 W | H2 | −68 a −72 dBc | 0.03981–0.02512 % |
| Bajo umbral | H3 | −78 dBc | 0.01259 % |
| Transición de rieles | H3 | −62 dBc | 0.07943 % |

Los valores espectrales quedan asociados a la condición de ensayo declarada: 1 kHz, 6 Ω y 50 W. No extrapolarlos automáticamente a toda frecuencia, potencia, temperatura o carga reactiva.

### Protección reportada

| Variable | Valor aportado |
|---|---:|
| Umbral de pico de corriente | >7.5 A pico |
| Tiempo de disparo por pico | <15 ms |
| Sobrecarga sostenida | 4.5–5.5 A durante aproximadamente 1.2 s |
| Red DET_DC | 47 kΩ y 47 μF |
| Constante RC | 2.209 s |
| Frecuencia de corte RC | 0.072 Hz |
| Offset DC de disparo | ±1.5 V |
| Tiempo de detección DC | 340 ms |

Los valores de protección se registran como datos de caracterización del equipo aportados por el usuario. No son umbrales universales del STK ni deben usarse para diseñar protecciones sustitutas sin validar el circuito físico.

### Conversión a coeficientes para simulación normalizada

Para generar un estímulo de simulación se adopta la convención x_peak = 1 en el punto de ensayo de 50 W / 6 Ω. Para el polinomio memoryless y = x + a2*x² + a3*x³, los armónicos de una senoide dan a2 = 2*H2 y a3 = 4*H3 (relaciones lineales de amplitud). Por tanto:

- a2 derivado del intervalo H2: 0.00050238–0.00079621.
- a2 central para −70 dBc: 0.00063246.
- a3 bajo umbral para −78 dBc: 0.00050357.
- a3 durante transición para −62 dBc: 0.00317731.

Estas constantes son coeficientes de un **modelo polinómico de prueba normalizado**, no kernels Volterra H2/H3 con memoria identificada. La fase de los armónicos no fue incluida en los datos aportados; el simulador usa fase cero solo como convención inicial y debe permitir sustituirla cuando exista medición compleja.

### Uso en IVANNA

El archivo app/src/main/cpp/neuromorphic/sony_hcd_pz1d_profile.hpp contiene los parámetros y las funciones auxiliares de simulación. El perfil se incorpora como modelo experimental separado: no modifica automáticamente el kernel H2 de producción ni habilita una ruta PAL/HAL. La máquina de estado Clase H es una aproximación discreta para simulación y no reemplaza el circuito analógico real.

### Límites y trazabilidad

- Los valores de esta sección se aceptan como mediciones aportadas por el usuario y se preservan sin sustituirlos por valores de datasheet.
- Los valores de hoja de datos del STK416-130-E siguen en la sección anterior como especificaciones del componente aislado; no deben mezclarse con las mediciones de la unidad Sony.
- El perfil conserva el identificador de equipo SONY_HCD_PZ1D, la condición de ensayo y la procedencia user_lab_measurements_2026-11-06.


## Integración en la ruta PCM real de AudioEffect

El perfil HCD-PZ1D ya no queda aislado como referencia offline. En la rama de integración:

- `omega_effect.cpp` instancia el perfil por contexto de efecto y carga `kA2Nominal` como coeficiente diagonal de retardo cero del kernel H2 de Volterra.
- Tras `processInterleaved()`, la ruta PCM aplica H3 con el valor medido correspondiente al estado Clase H.
- El estado usa el ancla de calibración declarada: PCM peak 1.0 representa 24.5 V pico en el ensayo 50 W / 6 Ω. El estado conserva el riel alto durante 12 ms (extremo conservador del intervalo medido).
- H3 comparte la envolvente de transición del motor Volterra para evitar un escalón al activar/desactivar el efecto.
- El estado es por instancia de AudioEffect. La arbitración GoldenEar, el bypass térmico y el control `OMEGA_FLAG_VOLTERRA_ON` siguen gobernando la autoridad del slot no lineal.

**Límite físico:** AudioEffect procesa muestras PCM y no controla eléctricamente los rieles VH/VL del amplificador Sony. La implementación aplica la firma armónica medida al flujo digital; no conmuta la fuente física del STK. La verificación final requiere instalar el APK/módulo y comprobar la salida real en el equipo.
