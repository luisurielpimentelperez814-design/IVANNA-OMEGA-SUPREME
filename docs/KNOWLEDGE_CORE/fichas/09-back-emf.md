# Ficha 09 — Back-EMF (fuerza contraelectromotriz)

**Dominio:** A/D (eléctrica/acústica) · **Estado:** interfaz lista, medición pendiente
**Fuentes:** `docs/HYPER_STATE_OPERATOR.md` §2; `FRONTERA.md` §2.2; Thiele & Small.

## Concepto

Un altavoz es también un generador: cuando su cono se mueve, genera una
tensión (back-EMF) proporcional a su velocidad (`e = Bl·v`). Midiendo tensión
y corriente (V/I) se estima el estado electromecánico real del driver: su
desplazamiento, su velocidad, su impedancia instantánea. Es la única vía para
saber lo que el altavoz HACE de verdad, no lo que se le pide que haga.
La no-linealidad del altavoz vive en tres curvas: `Bl(x)` (factor de fuerza),
`Kms(x)` (rigidez de la suspensión) y `Le(x)` (inductancia), todas dependientes
del desplazamiento.

## Definición y fórmulas

- **Back-EMF:** `e(t) = Bl · v(t)` (Bl en T·m, v en m/s).
- **Impedancia eléctrica:** `Z = Re + jω·Le + Bl²/Z_mecánica` (la parte
  mecánica se refleja en la eléctrica).
- **Parámetros Thiele-Small (defaults genéricos en Phi):** Re 6 Ω, Le 0.5 mH,
  Bl 5 T·m. Son valores genéricos de un driver pequeño, NO medidos de ningún
  modelo concreto.
- **Observador EMF:** estima desplazamiento/velocidad desde V/I con el modelo
  Thiele-Small (`EmfEstimator`).

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| `Z_emf(t)` en Phi | `HyperStateOperator.hpp` | `setMeasuredVI` / `setMeasuredEmf`. Sin fuente real, el término EMF es **identidad** (ningún modelo sustituye a la medición). |
| Interfaz | `neuro_acoustic_closed_loop.hpp` | `NeuroFrame` / `EmfEstimator`. |

**Restricción real:** en un teléfono Android NO hay vía eléctrica (el Aux
3.5 mm es solo salida, sin retorno). El altavoz se observa **acústicamente**
a través del micrófono (`SensorFrame`), no eléctricamente. La interfaz queda
para hardware con sensado de corriente.

## Mediciones que lo respaldan

- **Ninguna medición real aún en el G85.** Los parámetros por defecto son
  genéricos. Pendiente: estimar Re/Le/Bl del altavoz del G85 por vía acústica.
- El kernel Volterra medido (ficha 02) es la evidencia indirecta de la
  no-linealidad `Bl(x)`/`Kms(x)`: el kernel H2 captura su efecto.

## Preguntas abiertas (para la evolución)

1. ¿Se pueden estimar Re/Le/Bl del altavoz del G85 desde el micrófono
   (respuesta acústica + modelo) con precisión útil?
2. ¿El término EMF en Phi mejora algo si se alimenta con la estimación
   acústica en vez de ser identidad?
3. ¿Volterra por banda (ficha 02) + Back-EMF son dos caras del mismo
   fenómeno? ¿Un kernel Volterra estimado DESDE la impedancia?
4. ¿Protección del altavoz: detectar excursión excesiva por EMF y limitar
   antes de dañar el driver?

## Hueco 3 cerrado — Thiele-Small del G85 (2026-11-09)

**Estado: PARCIAL.** Fs es estimable con IVANNA: pico de resonancia (o codo −3 dB)
en los gains de `RoomMeasurement` a 25–250 Hz, teléfono a 10 cm (campo cercano),
validación cruzada con la cola de la ráfaga de RT60 a 125 Hz; valor esperado
150–350 Hz para un micro-altavoz. **Qts y Vas REQUIEREN EQUIPO EXTERNO** (único
hueco que lo exige): jig de impedancia casero (~$5–20) o Dayton DATS v3 (~$100).
Sin curva de impedancia no hay forma honesta de estimarlos. Detalle en
`docs/KNOWLEDGE_CORE/GAPS_FILLED.md` §3.
