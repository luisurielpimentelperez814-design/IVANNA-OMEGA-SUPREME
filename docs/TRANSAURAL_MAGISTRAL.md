# Transaural MAGISTRAL — sala del propietario en la ruta de audio real

Estado: **verificado en host (pruebas unitarias y de modelo), NO en dispositivo ni con micrófono binaural.**
No declarar "transaural validado" (ver `AGENT_CLAIMS.md`).

## Datos dados por el propietario

| Elemento | Valor |
|---|---|
| Sala | 3.83 m ancho × 2.83 m alto × 7.00 m largo |
| FL, FR | 1.50 m de altura, separados 2.70 m |
| SL, SR | sobre las paredes laterales, a 3.50 m del frente, a 1.80 m de altura |
| Subwoofer | a ras de suelo (y = 0) |

Marco de sala: x = ancho, y = altura, z = profundidad desde el frente. Orden de altavoces: FL, FR, SL, SR, TL, TR, SW.

## Supuestos míos (el propietario no los dio; cámbialos si no son ciertos)

- **Oyente:** sobre el eje central (x = 1.915 m), a 3.50 m del frente (alineado con SL/SR) y oídos a 1.20 m. Es el valor que el repo ya usaba para altura y profundidad; solo `x` cambió por el ancho de la sala.
- **FL/FR centrados:** x = 0.565 y 3.265 (simétricos respecto del eje).
- **TL/TR** (no mencionados): en el techo (y = 2.83 m), x = 0.25 y 3.58, z = 3.50. Antes estaban a 3.00 m, por encima del techo real.
- **Subwoofer:** x = 3.00, z = 3.00 (posición anterior, no cambiada); solo se bajó al suelo.
- **SL/SR a 1.80 m:** el mensaje decía 1.80 m y luego "1.50 m cada uno"; se tomó 1.80 m.

## Qué cambió en la ruta real

1. **Perfil de sala único.** `RoomGeometryConfig` (fuente), el snapshot por defecto (`omega_control_bus.h`), el estado del daemon (`command_server.h`) y la UI (`WfsCalibrationManager.kt`) llevan los mismos valores; `tests/test_magistral_defaults.py` impide que diverjan. `SET_WFS` (`wfsSpeakerX/Y/Z`) sigue pudiendo sobreescribirlos.
2. **La geometría del cancelador sale de la sala.** Antes, ningún camino fijaba `CrosstalkCanceller::Geometry`: ambos cancelladores corrían con la geometría de escritorio (0.90 m de separación, 0.85 m de distancia). Ahora `RoomGeometryConfig::crosstalkPlacement()` deriva separación, distancia (con la diferencia de altura altavoz–oído) y corrimiento lateral del oyente a partir de FL/FR del snapshot, y se aplica a:
   - el cancelador del motor de fusión (`IvannaFusionEngine::setWfsSpeakerLayout`), y
   - el de la cadena 4D (`fourd::setSpeakerGeometry`, ahora almacenado para sobrevivir a que la cadena aún no exista).
3. **`Geometry::listenerOffsetM`:** corrimiento estático del oyente que se suma al del rastreador de cabeza.

## Números derivados (del modelo del propio módulo)

| | Magistral | Escritorio (antes) |
|---|---|---|
| Separación / distancia | 2.700 m / 3.513 m | 0.90 m / 0.85 m |
| ITD del cruce | 183.0 µs (≈ 8.8 muestras a 48 kHz) | 237.9 µs |
| ρ (con el tope de +4 dB) | 0.369 | 0.369 |

El ITD de la sala coincide con la fórmula de Woodworth (≈ 185 µs) dentro del 12 %. ρ queda igual porque el tope de realce mono de +4 dB (`AGENT_CLAIMS.md`) es el que manda; lo que cambia es el retardo del cruce, 55 µs, que a 2 kHz son ≈ 40° de fase.

## Pruebas

`tests/test_magistral_room_xtc.cpp` (10 casos): coordenadas, altavoces dentro de la sala, copia del snapshot, derivación de la colocación, ITD frente a Woodworth, rechazo de colocaciones inválidas, oyente descentrado, cancelación contralateral ≥ 5 dB de 500 Hz a 4 kHz con esta geometría, mejora frente a la geometría de escritorio y realce mono dentro del tope con el sub intacto. `tests/test_magistral_defaults.py` (6 casos).

## Lo que NO está demostrado

- Que el oyente esté donde se supone, ni cuánto se mueve.
- Reflexiones de la sala (FL/FR a 0.565 m de la pared lateral) y de las paredes de hormigón: el cancelador no las modela.
- Sonido real: falta medir con micrófonos binaurales en el oyente.
- El cableado de `IvannaFusionEngine` y `omega_effect.cpp` solo se revisó por sintaxis en host (`IvannaFusionCore.cpp` y `FourDControlSurface.cpp` compilan sin errores); no se ejecutó en el dispositivo.
- El WFS recibe también el nuevo layout; su calidad en esta sala no se midió.
