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
