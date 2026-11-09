# EVOLUTION MAP — Cómo el Motor de Evolución usa el Knowledge Core

**Propósito:** este documento le dice al Motor de Evolución (el subagente que
construye la auto-evolución en código) CÓMO usar el Núcleo de Conocimiento
para proponer física nueva. No es una lista de deseos: es un protocolo con
criterios de muerte.

## 1. El ciclo de evolución (cómo se propone física nueva)

```
1. LEER el núcleo (query.py --context "<tema>") + el estado actual
   (tests, telemetría del teléfono, logs de fallos, directivas del usuario).
2. COMBINAR dos o más conceptos de dominios distintos (ver §2).
3. FORMULAR una afirmación falsable en una frase: "X predice/mejora Y
   medido como Z". Si no se puede formular así, no es una propuesta: es humo.
4. DEFINIR el experimento que la mataría (proxy objetivo + ABX, ficha 12).
5. ESCRIBIR el parche (código real, no documento) + test que lo cubre.
6. VALIDAR: tests host 847/847 + el proxy objetivo se mueve en host.
7. PUERTA: solo con GREEN replicado en ABX se enciende por defecto.
```

## 2. Combinaciones candidatas (cruces entre fichas)

Cada cruce es una hipótesis, no un resultado. El número entre paréntesis es
la prioridad de FRONTERA.md cuando existe.

| # | Cruce | Hipótesis falsable | Validación |
|---|---|---|---|
| E1 | Volterra (02) × Back-EMF (09) | El kernel Volterra estimado DESDE la impedancia (V/I) predice la THD real tan bien como el kernel medido acústicamente | Predicción <1 dB por armónico en señal no usada para identificar |
| E2 | HOLOS (03) × Torso/hombro (05+15) | Añadir torso + eco de hombro a G_b mejora el juicio de distancia ≥20% y la elevación <3 kHz | ABX distancia (0.15–1.0 m) + elevación <3 kHz |
| E3 | Phi (04) × MOC (26) | El lazo eferente en Γ_b mejora la detección de un tono 50–200 ms tras un enmascarador y retrasa la fatiga ≥15 min | Test de enmascaramiento + escucha 60 min |
| E4 | ORBE (05) × OT pinna (01) | La interpolación OT (Wasserstein) preserva los notches de pinna mejor que la lineal en HRTF reales | Error de posición del primer notch en HUTUBS (≥2 de 3 sujetos) |
| E5 | ASRC (06) × Beamforming (17) | El beamforming con el doble mic mejora la estimación de la sala y la coherencia por banda del sensor | Coherencia por banda medida con/sin beamforming |
| E6 | EvolutionaryEQ (10) × Harman (07) | El target individual de Esse (medido por ABX) difiere de Harman por banda; evolucionar hacia él sube la preferencia | ABX Harman vs target individual, niveles igualados |
| E7 | NPE (11) × ASSR (21+26) | La ASSR 40 Hz observa la coherencia espacial de HOLOS/ORBE y corrige Γ_b en segundos | Correlación ASSR ↔ coherencia espacial; ABX lazo abierto/cerrado |
| E8 | Enmascaramiento (13) × Compresión (18) | La compresión multibanda por ERB reduce la THD en graves sin tocar agudos | THD por banda con/sin multibanda |
| E9 | Separación (24) × YAMNet (22) | El stem vocal separado alimenta mejor al Voice Protection que el YAMNet | Tasa de falsos positivos de Voice Protection |
| E10 | Salas (16) × Sensor (04) | Estimar RT60 por banda de la sala real de Esse con el micrófono y renderizar con ella se percibe como "estar en esa habitación" | ABX sala estimada vs sintética + error RT60 <10% |
| E11 | Dithering (19) × Exciter (02) | Oversampling 4× en el exciter baja el aliasing medido sin subir el coste más de 2× | Aliasing medido con/sin 4× + throughput |
| E12 | Fatiga (26) × Sonoridad (14) | ∫e_b por banda predice la fatiga reportada mejor que el nivel (LUFS) solo | Correlación ∫e_b ↔ fatiga en sesiones reales |
| E13 | HRTF personal (01) × BEM (05) | HRTF por BEM desde foto de oreja supera a KEMAR en elevación en ≥2 de 3 sujetos | ABX elevación + confusiones delante/detrás |
| E14 | Anti-adaptación (FRONTERA §2.6) × NPE (11) | Pre-compensar la adaptación sináptica baja el umbral de detección de onsets ≥3 dB | Test de umbral de onset + ABX claridad en pasaje denso |

## 3. Qué es física nueva de verdad (tres niveles, no confundirlos)

| Nivel | Qué es | Ejemplo | Cómo se demuestra |
|---|---|---|---|
| 1 | **Física nueva**: un modelo que predice una cantidad medible que nadie ha medido | El kernel Volterra por banda del G85 predice la THD real mejor que el modelo genérico | Predicción verificada |
| 2 | **Ley psicofísica nueva**: una ley cuantitativa señal→percepción | LEV = f(IACC tardía por banda, DRR, trayectoria de coherencia) con coeficientes medidos | La ley predice juicios de oyentes nuevos (R²>0.7) |
| 3 | **Producto nuevo**: una implementación que nadie ha empaquetado así | HOLOS (un solo operador RT en todo el sistema) | Tests + puerta GREEN |

**El nivel 2 es el premio mayor y el más ignorado:** nadie tiene una ley
cuantitativa de la envolvencia para música estéreo. Medirla con disciplina
en el G85 es el trabajo con más probabilidad de ser un descubrimiento real,
y el que menos código nuevo exige: exige medir bien.

## 4. Criterios de muerte (cuándo archivar una propuesta)

- Si no mueve su proxy objetivo (ficha 12) en host, no se prueba en oídos.
- Si mueve el proxy pero no pasa el ABX con niveles igualados, se archiva
  como "física correcta sin valor perceptual".
- Si exige subir el nivel para sonar mejor, es trampa: se mide con niveles
  igualados o no se mide.
- **Nada se enciende por defecto sin GREEN, y nada se llama "descubrimiento"
  sin GREEN replicado en dos sesiones.**
- **Nunca se auto-modifica:** la seguridad anti-piratería, la integridad, los
  pins y el SafetyLimiter. Esos son límites duros, no parámetros.

## 5. Qué conocimiento le falta al núcleo (para completar)

| Hueco | Qué falta | Dónde conseguirlo |
|---|---|---|
| Audiograma de Esse | Umbral por frecuencia, ABR onda V | Medición clínica (una vez) |
| RT60 de la sala de Esse | Medición por banda con el micrófono | Sensor acústico + palmada |
| Parámetros Thiele-Small del G85 | Re/Le/Bl medidos | Estimación acústica (ficha 09) |
| Precisión de YAMNet en el G85 | % acierto voz/música/ruido real | Escucha etiquetada |
| Latencia extremo a extremo | Por ruta (altavoz/USB/BT) | Medición en dispositivo |
| Ley de envolvencia | Coeficientes LEV = f(IACC, DRR) | Puerta auditiva sistemática |
| Kernel Volterra por banda | Medición en vivo (no tabulado) | Programa GENEZY (ficha 02) |
