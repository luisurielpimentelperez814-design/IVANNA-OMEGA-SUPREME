# FLANCO: Motor de percepcion SAF + HRTF binaural
Owner: agente de percepcion (reservado en docs/AGENT_COORDINATION.md)

## Estado del arte objetivo
Renderizado binaural de referencia (nivel Apple Spatial / Sony 360RA / Resonance):
convolucion HRTF particionada, interpolacion de magnitud, ITD interaural
explicito, crossfade de potencia constante, cero denormales en el hilo DSP.

## Mejoras aplicadas (2026-09-07)
1. Anti-denormales FTZ/DAZ por hilo en HRTFConvolver::process (SSE y AArch64 FPCR.FZ)
   -> elimina el pico de 10-100x de CPU cuando las colas del filtro decaen a subnormales.
2. ITD interaural (kMaxItdSamples, modelo Woodworth, head radius 0.0875 m):
   la lateralizacion ya no depende solo de la magnitud HRTF; se anade delay
   fraccional interaural — la pista de localizacion dominante por debajo de ~1.5 kHz.
3. Crossfade de potencia constante ya existente (auditado, correcto).

## Roadmap del flanco (siguientes sesiones)
## Sesion 2 (2026-09-08): ITD interaural IMPLEMENTADO
- computeItdSamples(): Woodworth (r/c)(sin+theta), clamp a kMaxItdSamples=64.
- applyItd(): delay fraccional (interp. lineal) por oido al final de process(),
  one-pole ~1.5 ms anti-zipper, camino rapido sin-delay, reset() limpia estado.
- Convencion: azimuth>0 = fuente a la derecha -> oido izquierdo retrasado.

## Roadmap del flanco (siguientes sesiones)
## Sesion 3 (2026-09-09): convolucion particionada no uniforme IMPLEMENTADA
- RirConvolver: head (512, latencia 0) + cola (hasta 16384 muestras = 341 ms @48k)
  en 32 particiones con FDL (frequency-delay-line) y suma espectral por bloque.
- load() segmenta el IR en hilo de control (fft seguro fuera de process()).
- La reverb de sala real ya no esta limitada a 512 muestras (era inutilizable
  para espacios reales: 512 @48k = 10.7 ms).

## Roadmap del flanco (siguientes sesiones)
- (HECHO) Convolucion particionada no uniforme (latency-0 head + tail).
## Sesion 4 (2026-09-11): BRIR con cola tardia decorrelada IMPLEMENTADO
- setLateDecorrelation(amount): red allpass en frecuencia sobre la cola R.
  |H|=1 (preserva RT60 y nivel), solo rota fase por bin -> colas L/R incoherentes.
  La reverb deja de ser una imagen mono fantasma y se vuelve difusa/envolvente.
- Cableado en load() (hilo de control, cero coste en process()).

## Roadmap del flanco (siguientes sesiones)
- (HECHO) BRIR con reverberacion tardia decorrelada.
- Personalizacion HRTF desde SAF latente (q_t) -> seleccion de dataset por usuario.
- Validacion: medicion de ILD/ITD contra base CIPIC/KEMAR de referencia.
