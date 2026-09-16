# HOA → Binaural → Upmixing Pipeline Implementation

Este documento detalla la implementación solicitada por el usuario (2026-09-15) basada en la realidad del código en `main` (commit 06defa02).

## Fase 1: Upmixing (IntelligentUpmixer)
- **Descomposición Mid-Side Real**: No hay "clasificador de centros". Se usa matemática M-S real (`mid = (L+R)/2`, `side = (L-R)/2`).
- **Filtrado de Graves**: Separación LPF estricta (150Hz) sobre la señal `mid` usando un filtro biquad real, no redes neuronales, tal como exigía la verdad del repositorio.
- **Transientes**: Integración de `TransientDetector` puro (envelope follower), sin dependencias CRNN fantasma.
- **Encoding HOA**: Se inyecta la señal `mid-high` al frente (`0` radianes) y los `sides` ensanchados usando el `HoaGainMatrix` real (SN3D/ACN).

## Fase 2: Rendering Binaural (HoaBinauralDecoder)
- **HRTF 2D**: Se obedece la restricción técnica del `HRTFConvolver::set_position`, el cual solo soporta azimut. El anillo virtual es estrictamente horizontal (elevación 0).
- **Procesamiento**: Decodificación de los armónicos HOA hacia N (default 8) altavoces virtuales y posterior convolución estéreo mediante `HRTFConvolver`.
- **max-rE weighting (2026-09-15, refinamiento post-build-verde)**: el decoder original usaba muestreo puro (peso fijo 1/2/2 por orden), que reconstruye el campo exacto solo en las direcciones de los altavoces virtuales y deja rizado de energía entre ellas. Se añadió la ponderación max-rE estándar (Daniel & Nicol; misma técnica que usan YouTube 360, Facebook 360 y los decoders de IEM para auriculares): `g_m = cos(m·π/6)` por orden, con una renormalización que deja la ganancia en eje IDÉNTICA a la del decoder anterior — no cambia el volumen percibido, solo aplana el patrón de energía fuera de eje. Expuesto como `HoaBinauralDecoder::computeMaxReOrderWeights()` (estático, puro) y verificado con `HoaBinauralDecoderTest.MaxReWeightingPreservesOnAxisGainAndTapersHigherOrders` — invariante analítica exacta, no una aserción de "energía > 0".

## Fase 3: Integración Core (IvannaFusionCore)
- **Pipeline de Audio**: En el ciclo `process()`, tras la clasificación, se extrae el flujo del HRTFManager y se inyecta la dupla `Upmixer -> HOA Field -> Decoder`.
- **GoldenEar / Soft Clipping**: Mantenido tras la decodificación binaural, preservando la integridad de ganancia de salida.

## Estado Final
- Build estable (local verification y CMake targets añadidos).
- Estructura pura de DSP, desmintiendo wrappers mágicos (no hay IA donde no debe haberla).
