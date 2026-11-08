# Pilares pendientes — lo que se pudo hacer, y lo que falta (v2.5.368)

Este documento formula, pilar por pilar, el camino real hacia lo que
en v2.5.366 se declaró imposible o parcial. Cada sección dice: qué se
construyó ahora (código + tests), qué dice la literatura, y qué queda
fuera del alcance de una app de audio (hardware, medicina, física).

## Pilar 1 — Volterra K=4096: CONSTRUIDO (modelo diagonal)

**Antes:** el H2 completo es O(K²): K=4096 → 8,390,656 coef., ~806 GMAC/s.
Inviable. **Ahora:** `neuromorphic/diagonal_volterra.hpp` —
`DiagonalVolterra`, polinomio de memoria de orden 2:

```
y[n] = Σ_k h1[k]·x[n-k] + Σ_k h2d[k]·x[n-k]²
```

Coste O(K): K=4096 → 8,192 coef., ~0.79 GMAC/s en estéreo @48 kHz.
Viable. La literatura de DPD lo respalda: el polinomio de memoria es
la simplificación estándar del Volterra (Ding et al.), y la reducción
de desviación dinámica (DDR, Ghannouchi/Hammi) poda los términos
cruzados lejanos — un DDR de 2º orden usa 93 coeficientes frente a
1287 del Volterra completo (5º orden, 8 taps). La patente US
2025/0105865 separa los mismos términos: diagonales a_{k,m} y
cruzados b_{k,l}.

**Qué captura:** no-linealidad estática + memoria lineal larga
(resonancias de caja y modos del habitáculo hasta 85 ms). **Qué no:**
los cruzados h2[k,l], k≠l. El modelo completo del repo es la suma:
`VolterraH2Symmetric` K=64 (memoria corta, cruzados) +
`DiagonalVolterra` K=4096 (memoria larga, diagonal) = DDR de 2º orden.
Los coeficientes solo vienen de medición (protocolo GENEZY01);
`setCoefficients()` rechaza no finitos y |c|>8.

## Pilar 2 — Back-EMF: ESTIMADOR CONSTRUIDO, MEDIDA PENDIENTE

**Antes:** imposible por el Aux (sin camino de retorno). **Ahora:**
`spatial/EmfEstimator.hpp` — el estimador Thiele-Small, listo para
una fuente real de V/I:

```
V(t) = Re·I(t) + Le·dI/dt + Bl·v(t)   →   v = (V − Re·I − Le·dI/dt)/Bl
x(t) = ∫ v dt  (integrador con fuga)  ·  F(t) = Bl·I(t)
```

Verificado en host con V/I sintéticos: recupera la velocidad con
error RMS <5% y el desplazamiento con su amplitud teórica. **Lo que
falta es el HARDWARE de medida:** un amplificador con sensado de
corriente (Class-D con diagnóstico de carga / shunt + ADC) o una
interfaz USB con entrada de línea sobre un shunt en bornes. El DAC
USB-C por el Aux nunca podrá dar V/I: no hay ADC en ese camino. El
estimador no finge medir nada: sin fuente, salida cero.

## Pilar 3 — "Colapso de paredes": FORMULADO (control multipunto)

Con 2 altavoces es imposible (v2.5.366 §3). El camino real es el
**control de campo sonoro con array + micrófonos** (literatura de
sound field control / active acoustic control): N fuentes, M puntos
de control, presión objetivo d. Se resuelve por mínimos cuadrados
con regularización (pressure matching):

```
min_q  ||H·q − d||² + β·||q||²
q = (Hᴴ·H + β·I)⁻¹ · Hᴴ·d
```

donde H es la matriz M×N de respuestas fuente→punto (medida con
micrófonos), q los filtros de cada fuente, β la regularización que
evita ganancias absurdas fuera de los puntos de control. Esto es
exactamente la extensión del WFS que ya existe en el repo
(`WfsRenderer`): pasar de renderizar un campo a CONTROLARLO en M
puntos medidos. Requiere: un array de altavoces (no 2), M micrófonos
de medida, y la matriz H medida en el recinto. **No implementado en
v2.5.368** (es un módulo de control MIMO con su propio protocolo de
medición); la fórmula y los requisitos quedan aquí para no volver a
pedir "dos bocinas que cancelen las paredes".

## Pilar 4 — Elevación por altavoces: CONSTRUIDO (pinna-EQ)

**Antes:** HRTF wet=0 en AUX por diseño; faltaba el módulo. **Ahora:**
`spatial/PinnaElevationEq.hpp` — EQ paramétrico con las bandas
direccionales de Blauert (1997, pp. 108-111; Lee 2015; Hebrank &
Wright 1974):

```
frente: 300-500 Hz y 3-4 kHz  ·  atrás: 1 kHz (y 10-12 kHz)  ·  arriba: 8 kHz
```

Presets: Above (+8 kHz, −4 kHz), Front (+4 kHz, +500 Hz), Behind
(+1 kHz, +10 kHz), Flat (bypass bit-exacto), con `amount` 0..1 y
ganancias acotadas a ±3 dB. Los tests verifican la magnitud relativa
entre bandas (la pista es la energía relativa, no el valor absoluto
de una banda aislada). **Límites honestos:** solo aumenta la
probabilidad de percibir la dirección; varía entre oyentes y más aún
por altavoces que por audífonos; necesita señal de banda ancha con
agudos. No es "engaño al SNC", es un sesgo espectral documentado.

## Pilar 5 — Zero-PCM / DSD: CONSTRUIDO (DoP v1.1)

**Antes:** "DSD directo" no existe en el stack. **Ahora:**
`spatial/DopPacketizer.hpp` — el estándar abierto DoP v1.1 (Koch /
McHarg / Robinson, 2012): cada muestra PCM de 24 bits lleva 16 bits
de DSD en los bits 15..0 (bit más antiguo en el 15) y un marcador en
los bits 23..16 que alterna 0x05/0xFA. Portadora = DSD_rate/16:
DSD64 → 176.4 kHz, DSD128 → 352.8 kHz, DSD256 → 705.6 kHz. Los bits
DSD llegan bit a bit idénticos; el DAC DoP detecta la alternancia y
encamina a su decodificador DSD. **Límites honestos:** es TRANSPORTE,
no conversión — necesita fuente DSD (DSF/DFF) y DAC que declare DoP;
sin DAC DoP el flujo se oye como PCM con ruido. La conversión
PCM→DSD (modulador sigma-delta) NO está aquí. El DoP debe ir directo
al DAC sin DSP encima (volumen/EQ destruirían el marcador). La salida
Aux 3.5 mm sigue siendo analógica después del DAC.

## Los 4 paradigmas (de v2.5.367 §4): qué haría falta, formulado

- **Transducción no mecánica:** tFUS es neuromodulación médica en
  ensayos (UCSF MESSI-LEO NCT07710989); oCI exige terapia génica AAV
  + implante óptico (Moser/Gotinga). Para que fueran "audio" haría
  falta: un mapa audio→parámetros de sonicación con límites de
  seguridad (ISPTA) validados, o un implante óptico aprobado con su
  procesador externo. Nada de eso existe como producto; son
  dispositivos médicos.
- **Teleportación perceptual:** el camino real es captura HOA remota
  + streaming binaural. La "fusión multilocal" no es un problema
  definido: dos campos en el mismo aire se suman.
- **Audición hiperdimensional:** la sonificación de datos es real;
  "oír 4D/5D" no tiene evidencia. Prerrequisito inexistente: un
  protocolo de entrenamiento perceptual con métricas de aprendizaje
  en humanos.
- **Sincronización inter-cerebral:** las interfaces cerebro-cerebro
  existen en laboratorio (BrainNet 2019). Un espacio colectivo
  sub-milimétrico requeriría lectura/estimulación neural por
  participante + sincronización de reloj entre cerebros: no existe
  ni como experimento.

## Versión
v2.5.368 (658 → 659). Módulos + tests 11/11 nuevos (ASan+UBSan);
fusión 7/7, AUX 6/6, neuro 11/11 sin regresión. Commit y tag locales;
bundle fino regenerado para el push. Push a GitHub pendiente de la
vía del bundle (OAuth roto para el usuario; token expuesto revocar).
