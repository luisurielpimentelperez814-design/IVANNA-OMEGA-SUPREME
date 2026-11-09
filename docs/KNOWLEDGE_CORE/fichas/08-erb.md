# Ficha 08 — ERB (bandas críticas / escala ERB)

**Dominio:** A (psicoacústica) · **Estado:** en producción (24 bandas)
**Fuentes:** Glasberg & Moore (1990); `FUSION.md` §1.4; `VEREDICTO.md` §5.

## Concepto

La cóclea no oye en Hz lineales: oye en **bandas críticas** (filtros
auditivos). La escala ERB (Equivalent Rectangular Bandwidth) describe el
ancho de esos filtros: estrechos en graves, anchos en agudos. Procesar por
bandas ERB (en vez de por Hz o por octavas) es procesar como oye el oído.
Es la rejilla de análisis de ORBE, HOLOS, Phi y el sensor acústico.

## Definición y fórmulas

```
ERB(f) = 21.4 · log10(1 + 0.00437·f)     [Glasberg & Moore, Hz]
```

- ERB a 100 Hz ≈ 35 Hz; a 1 kHz ≈ 132 Hz; a 4 kHz ≈ 456 Hz; a 10 kHz ≈ 1100 Hz.
- **24 bandas ERB** entre 60 Hz y 16 kHz (o `min(16 kHz, 0.45·sr)`).
- Escala relacionada: **Greenwood** (posición en la cóclea ↔ frecuencia),
  usada para las bandas del NPE y del sensor neural.
- La resolución temporal y frecuencial están acopladas: una banda estrecha
  (graves) necesita una ventana larga; una banda ancha (agudos) resuelve
  transitorios finos.

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| ORBE | `spatial/OrbeBinaural.hpp/.cpp` | 24 bandas ERB (o 12), estimación por banda. |
| HOLOS | Núcleo HOLOS | 24 bandas ERB, STFT hop 128 / FFT 512. |
| Phi | `HyperStateOperator.hpp` | 24 bandas ERB, muestra a muestra. |
| Sensor acústico | `RealAcousticSensor` | 24 bandas ERB, FFT 8192. |
| CochlearActiveInverseEngine | 8 bandas Greenwood | Heun 10/80 ms. |

## Mediciones que lo respaldan

- ORBE: THD+N −139.33 dB @1 kHz con 24 bandas; 23× RT.
- Phi: 41–42× RT con 24 bandas. Sensor: 280× RT.
- El enmascaramiento es máximo DENTRO de la misma banda ERB (ver ficha 13).

## Preguntas abiertas (para la evolución)

1. ¿24 bandas o 12 en el G85 bajo carga térmica? ¿Cuánto se pierde con 12?
2. ¿Bandas ERB con solapamiento (como la cóclea real) en vez de
   partición dura? ¿Mejora la estimación de escena de ORBE?
3. ¿ERB individual (la anchura de banda varía entre oyentes ±20%)?
4. ¿La escala Greenwood es la correcta para mapear Γ_b al perfil neural?
