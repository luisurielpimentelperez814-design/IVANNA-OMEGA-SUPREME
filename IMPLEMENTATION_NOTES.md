# HOA → Binaural → Upmixing Pipeline Implementation

Documento reescrito 2026-09-16 tras verificar cada afirmación anterior
contra el código real, no contra lo que se suponía que debía existir. La
versión previa de este documento afirmaba en la Fase 3 que el pipeline ya
estaba "inyectado" en `process()` — eso era falso: `m_upmixer`/`m_hoaDecoder`
se preparaban en el constructor de `IvannaFusionEngine` pero ningún código
los invocaba nunca. Este documento corrige esa afirmación y describe lo que
de verdad hay, verificado por lectura completa del código fuente real.

## Fase 1: Upmixing (`spatial/IntelligentUpmixer.hpp/.cpp`) — real
- **Sin clasificador de IA.** Descomposición mid-side real (`mid=(L+R)/2`,
  `side=(L-R)/2`) — matemática determinista, no un modelo entrenado.
- **Imagen estéreo preservada como par exacto a ±30°** — `HoaGainMatrix::encode()`
  es una identidad matemática en el plano horizontal para ese caso, no una
  aproximación.
- **Crossover complementario de 2º orden** sobre el mid: `bass + agudos == mid`
  exacto muestra a muestra (sin error de fase en el corte). El grave se
  codifica al centro — mono-seguro (no se cancela al sumar a mono).
- **Transientes**: `TransientDetector` real (envelope follower/onset) sobre
  el mono verdadero, sin dependencia de ningún CRNN (nunca existió en este
  repo — verificado exhaustivamente contra main y las ~40 ramas remotas
  antes de empezar esta labor).
- **Inmersividad** suavizada por muestra (sin zipper al mover el control),
  sin malloc en el camino caliente salvo el primer dimensionado.
- Tests: 7/7 (`IntelligentUpmixerTest.*`), incluidos en la puerta CTest.

## Fase 2: Rendering binaural (`spatial/HoaBinauralDecoder.hpp/.cpp`) — real
- **HRTF 2D, honesto sobre su límite**: `HRTFConvolver::set_position()` solo
  admite azimut — el anillo de altavoces virtuales (8 por defecto) es
  estrictamente horizontal (elevación 0). No hay elevación real en ningún
  punto de este pipeline.
- **max-rE weighting (2026-09-15, refinamiento post-build-verde)**: el
  decoder original usaba muestreo puro (peso fijo 1/2/2 por orden), que
  reconstruye el campo exacto solo en las direcciones de los altavoces
  virtuales y deja rizado de energía entre ellas. Se añadió la ponderación
  max-rE estándar (Daniel & Nicol; misma técnica que usan YouTube 360,
  Facebook 360 y los decoders de IEM para auriculares): `g_m = cos(m·π/6)`
  por orden, con una renormalización que deja la ganancia en eje IDÉNTICA
  a la del decoder anterior — no cambia el volumen percibido, solo aplana
  el patrón de energía fuera de eje. Expuesto como
  `HoaBinauralDecoder::computeMaxReOrderWeights()` (estático, puro) y
  verificado con
  `HoaBinauralDecoderTest.MaxReWeightingPreservesOnAxisGainAndTapersHigherOrders`
  — invariante analítica exacta, no una aserción de "energía > 0".
- Decodifica los 9 canales HOA a cada altavoz virtual y convoluciona con
  `HRTFConvolver`; sin perfil HRTF personalizado propagado (`setHrtfProfile()`
  no se llama desde el motor: `HrtfManager` no expone hoy un
  `shared_ptr<SyntheticHRTF>` compartido con el que alimentarlo), cada
  convolver usa su propio respaldo sintético interno (Woodworth + sombra de
  cabeza) — comportamiento seguro y explícitamente permitido por el encargo
  original ("sin perfil HRTF cargado → HRTF genérico ya existente como
  respaldo"), documentado aquí como decisión, no como hueco pendiente.
- Tests: incluidos en la puerta CTest (round-trip de azimut, W-puro →
  energía igual en todos los altavoces, invariante de max-rE weighting).

## Fase 3: Integración real en `IvannaFusionEngine::process()` — cerrada 2026-09-16
**Corregido en esta sesión** (antes NO estaba cableado pese a lo que decía
la versión previa de este documento): `process()` ahora lee
`g_upmixing_enabled`/`g_upmixing_immersivity` cada bloque (mismo patrón ya
usado para `g_hrtf_wet_dry` unas líneas arriba en el mismo archivo) y los
aplica a `m_upmixer` antes de decidir la ruta:

- **Upmixing activo** → `m_upmixer.processBlock()` produce el campo HOA en
  `m_hoaField` (buffer reusado, sin malloc en régimen estable) →
  `m_hoaDecoder.processBlock()` lo decodifica a binaural, **reemplazando**
  la llamada a `HrtfManager::processBinauralScene()` para ese bloque — son
  la misma etapa conceptual del pipeline (espacializar el estéreo a
  binaural); aplicar ambas sería espacializar dos veces, no una mejora.
- **Upmixing inactivo** (el caso por defecto: `g_upmixing_enabled` arranca
  en `false`) → comportamiento sin cambios, `processBinauralScene()` de
  siempre.
- Soft-clip / GoldenEar se mantiene después, sin cambios — actúa sobre
  `buffer->left/right` sin que le importe cuál de las dos rutas los llenó.
- Verificado por compilación real (no solo lectura): `g++ -fsyntax-only
  -Wall -Wextra` limpio tanto en `IvannaFusionCore.cpp` (Ruta A) como en
  `omega_effect.cpp` (Ruta B, que incluye el mismo archivo vía
  unity-build — ambos procesos comparten el código, no los atomics: ver
  `upmixing_controls_bridge.cpp` para por qué existen dos copias de los
  controles). Puerta CTest completa: 96/96 verde tras el cambio.

## Qué es real y qué NO (obligatorio, sin lenguaje de marketing)
**Es real:** upmixing estéreo→HOA por mid-side + crossover de graves
determinista, decodificación a N altavoces virtuales horizontales, todo
binaural vía `HRTFConvolver` real (sintético o con dataset medido según
disponibilidad), ahora sí ejecutándose en el camino de audio en vivo
cuando el usuario lo activa.

**NO es real:** no hay elevación real en ningún punto (toda la "altura"
percibida, si la hay, sería ilusión espectral de otro subsistema, no de
este); la separación de "centro/lados/graves/transientes" es
procesamiento de señal clásico determinista, no separación de fuentes por
IA — llamarlo así sería impreciso; no hay WFS; orden Ambisonics >2 no
existe; este pipeline no reconstruye la grabación original, la deriva de
una mezcla estéreo ya mezclada.

## Estado final
- Fases 1, 2 y 3 completas, cableadas de verdad y verificadas por
  compilación + 96/96 tests, no solo por lectura de intención.
- Fase 4 (este documento) y Fase 5 (clipping bajo ASan/UBSan + medición
  real de latencia) — ver `AGENT_CLAIMS.md` para el estado de la
  verificación de Fase 5, que se hace por separado y se anota ahí, no aquí.
