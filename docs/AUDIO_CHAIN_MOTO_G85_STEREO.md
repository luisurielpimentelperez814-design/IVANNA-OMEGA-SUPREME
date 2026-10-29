# Moto G85 → USB DAC → auxiliar estéreo → Sony

## Contrato de salida

Manual aportado por el propietario: **Sony MHC-PZ1D**, documento `4-277-663-33(1)`, archivo `42776633M.pdf` (115 páginas). Consultadas las páginas 1, 11–12, 17–19, 24, 67–69, 79–80 y 97–99. No hace falta inferir el modelo a partir del nombre escrito anteriormente.

La entrada **VIDEO/SAT AUDIO IN L/R** transporta audio analógico estéreo (p.79). El conjunto de fábrica tiene **dos delanteros, dos satélites y un subwoofer** (pp.17,99). El manual aclara que los satélites emiten el mismo sonido que los delanteros (p.69): no son canales surround discretos independientes. Si se han añadido otras bocinas, su conexión e impedancia requieren confirmación; no se modelan como nuevas salidas del DAC.

### Controles reales del Sony

| Control | Función documentada |
|---|---|
| VIDEO/SAT o FUNCTION | Selecciona la entrada analógica VIDEO/SAT (p.80). |
| SOUND FIELD: 2CH STEREO | Para funciones distintas de DVD conserva el sonido original (p.68). |
| SOUND FIELD: 2CH SURR | Añade el efecto envolvente del Sony también fuera de DVD (p.68). |
| VIRTUAL A / VIRTUAL B | El manual limita su eficacia a fuentes multicanal; no se prescriben como reconstrucción multicanal desde AUX L/R (pp.68–69). |
| SUBWOOFER | Enciende/apaga el subwoofer; el indicador muestra su estado físico (pp.12,67). |
| GROOVE / Z-GROOVE | Cambian refuerzo, volumen y curva EQ (p.67). Para comparación inicial: GROOVE OFF y PRESET EQ FLAT, manteniendo nivel igualado. Esta última elección es un protocolo de comparación, no una promesa de mejor sonido. |

La entrada se especifica con **250/450 mV y 47 kΩ** (p.98). Sony indica cambiar de VIDEO a SAT si la señal resulta demasiado alta o distorsiona (p.80); el cambio se realiza manteniendo VIDEO/SAT y pulsando STOP. El modelo y nivel de salida del DAC siguen pendientes: no se prescribe un porcentaje fijo de volumen ni un techo digital que garantice evitar saturación analógica sin medirlo.

```mermaid
flowchart LR
  A[YouTube / Spotify / otra app] --> B{Ruta de audio verificada}
  B -->|Efecto global Magisk activo| C[Procesamiento IVANNA]
  B -->|Captura permitida por Android y la app| D[Captura autorizada]
  D --> C
  C --> E[DAC USB-C: dos canales]
  E --> F[AUX analógico L/R del Sony]
  F --> G[Surround del Sony: botón físico]
  G --> H[Bocinas conectadas al Sony]
  F --> I[Subwoofer: botón y filtro del Sony]
```

Las rutas global y captura no deben procesar simultáneamente la misma señal. Una conexión USB detectada no prueba el endpoint realmente utilizado por AudioPolicy ni la inserción del efecto global. Algunas aplicaciones impiden captura; no se garantiza cobertura de cualquier plataforma sin comprobar su ruta real.

## Perfil añadido

En **Bocinas declaradas**, seleccionar **Equipo estéreo externo (Sony u otro)**. Se guarda la declaración independientemente del número de bocinas. En USB/AUX, RouteDspCalibrator desactiva la HRTF del procesador legacy, elige el arquetipo de altavoces y no añade la sala sintética/optimización de sala por defecto. La pantalla deja de publicar un layout WFS multicanal al activar el surround declarado sobre esta conexión estéreo.

Esto no es una calibración del Sony ni una certificación de todos los renderers. Los otros paneles permiten configurar motores adicionales; hay que verificar el estado efectivo del pipeline global antes de escuchar. El subwoofer declarado no crea LFE, crossover ni control remoto del equipo. El perfil de auriculares sigue siendo independiente.

## Aceptación física pendiente

1. Identificar el DAC y confirmar VIDEO/SAT AUDIO IN L/R del MHC-PZ1D, altavoces conectados y disponibilidad de Magisk.
2. Confirmar el endpoint USB en reproducción; L/R correctos y canales sin invertir.
3. Confirmar inserción del efecto con cada aplicación y ausencia de captura duplicada.
4. Comparar bypass frente a procesamiento con volumen igualado: primero surround Sony apagado y luego encendido; no atribuir al software el efecto del Sony.
5. Medir clipping, ruido, latencia y estabilidad al reconectar USB; evaluar localización frontal/lateral, profundidad y fatiga desde la posición de escucha.
6. Probar subwoofer apagado/encendido y medir integración con las bocinas. No asumir que más graves significan mejor localización.

«Holofonía 4D» es aquí un objetivo perceptual de espacialidad, profundidad y movimiento. La matemática validada y el cableado de controles son condiciones necesarias; no garantizan una experiencia universal ni superioridad frente a otros productos.
