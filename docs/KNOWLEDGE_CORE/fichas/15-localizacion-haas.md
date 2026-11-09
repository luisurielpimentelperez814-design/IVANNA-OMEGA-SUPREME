# Ficha 15 — Localización y efecto Haas (precedencia)

**Dominio:** Psicoacústica avanzada · **Estado:** usado en el motor (ITD/ILD, XTC)
**Fuentes:** `VEREDICTO.md` §5; `FUSION.md` §1.1; Blauert (1983); Litovsky et al.

## Concepto

El cerebro localiza una fuente por tres claves: ITD (tiempo), ILD (nivel) y
pinna (espectro, elevación). Pero cuando el mismo sonido llega dos veces
(directo + reflexión), el cerebro localiza por el PRIMERO y suprime el
segundo: es el **efecto Haas o de precedencia**. Es lo que permite oír en una
sala reverberante sin que cada reflexión sea una fuente fantasma. El efecto
tiene una ventana: si el segundo sonido llega <1 ms después, se fusiona
(suma de sonoridad); entre 1–30 ms se localiza por el primero; entre 30–50 ms
se oye como eco separado; más allá, como reverberación.

## Definición y valores

- **ITD:** Woodworth, máx. ~0.655 ms (ficha 01). Resolución (JND) ~10–20 µs.
- **ILD:** JND ~1 dB. En graves, casi nulo en campo lejano.
- **Efecto Haas:** ventana de fusión ~1 ms; supresión del eco hasta ~30 ms
  (el eco puede ser hasta 10 dB más fuerte y seguir suprimiéndose);
  eco audible >50 ms.
- **Conos de confusión:** ITD+ILD solos no distinguen delante/detrás ni
  arriba/abajo (mismo ITD/ILD en un cono). La pinna y el movimiento de cabeza
  resuelven la ambigüedad.
- **XTC (cancelación de diafonía):** en altavoces, cada oído oye también el
  altavoz contrario con ~0.2 ms de retardo. XTC cancela esa diafonía para
  que el estéreo en altavoces se oiga como binaural.

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| XTC 4D | Cadena Ruta A/B | Cancelación de diafonía, solo altavoces. |
| Crossfeed ITD | `HeadphoneCrossfeed.hpp` | Woodworth 0.261 ms @30°, LP 700 Hz. |
| ORBE/HOLOS | Estimación de escena | ITD/ILD medidos por banda para azimut/distancia. |
| WFS | `WfsRenderer.hpp` | ITD entero por altavoz (máx. 30 muestras @48 kHz). |

## Mediciones que lo respaldan

- Crossfeed: −6 dB de separación a 100 Hz medidos (`test_headphone_crossfeed`).
- ORBE: ITD medido vs Woodworth+corrección esférica (±5 µs, test).
- **Sin medición propia del efecto Haas en el G85** (ventana de fusión en
  sala real con el motor).

## Preguntas abiertas (para la evolución)

1. ¿La ventana de Haas del oyente (Esse) es la estándar (30 ms) o distinta?
   ¿Afecta al RT60 óptimo de la RIR?
2. ¿XTC adaptativo por banda (no banda ancha) mejora la externalización en
   altavoces sin artefactos?
3. ¿El movimiento de cabeza (head-tracking) resuelve los conos de confusión
   mejor que la pinna sintética?
