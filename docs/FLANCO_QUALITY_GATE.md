# FLANCO: Quality Gate de percepcion (validacion binaural objetiva)
Owner: este agente (reservado abajo). Fecha: 2026-09-11.

## Objetivo
Cerrar el bucle de calidad del motor binaural con MEDICION, no solo sintaxis:
harness host que sintetiza escenarios y mide metricas objetivas.

## Metricas implementadas
- ITD: barrido de azimut -> ITD medido vs teorico (Woodworth), error max.
- ILD: nivel interaural por banda, contra referencia de cabeza esferica.
- Decorrelacion de cola: coeficiente de correlacion L/R de la reverb tardia
  (objetivo: < 0.3 con setLateDecorrelation(1.0), > 0.9 con 0.0).
- Piso de denormales: tiempo CPU del convolver con colas subnormales
  (objetivo: sin pico > 2x respecto a señal normal -> FTZ activo).

## Por que esto lo separa del estado del arte
Los motores de referencia (Resonance, 360RA) se validan asi: no por "compila",
sino por "el ITD medido a 30deg esta dentro de X muestras del teorico".
Este harness convierte la calidad binaural en un gate objetivo y repetible.
