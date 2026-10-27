# Sony Genezi MHC-PZ1D — perfil AUX conservador y modelo de firma

## Alcance y evidencia

Este perfil está diseñado para un Sony Genezi MHC-PZ1D/HCD-PZ1D conectado a IVANNA por entrada analógica estéreo. Confirmar la etiqueta del equipo antes de atribuirle esta documentación; las variantes regionales pueden diferir.

El manual de servicio publica una entrada VIDEO/SAT AUDIO IN L/R de 47 kΩ y sensibilidad especificada de 250/450 mV, así como etapas/salidas dedicadas para delanteros, satélites y subwoofer. El microcontrolador expone controles separados para relé/mute delantero y relé/mute del subwoofer. Esto confirma control de estado independiente, pero NO prueba que el botón del subwoofer aumente eléctricamente la ganancia de los satélites.

Fuente de referencia: Sony HCD-PZ1D Service Manual, sección de especificaciones, diagrama de bloques de audio y funciones de pines del IC501 (MAIN board).

## Observación del usuario que guía la hipótesis

Al habilitar SURROUND y después el subwoofer, el usuario percibe que los satélites reciben más energía. Se conserva como observación auditiva, no como medición eléctrica. Las hipótesis abiertas son: cambio de ganancia, cambio de filtrado/crossover, conmutación de etapa/alimentación o cambio perceptivo por balance tonal.

## Modelo paramétrico

Para entrada estéreo x(f) = [L(f), R(f)]ᵀ, se representa el sistema como:

y(f,s,b) = H(f,s,b) x(f)

donde s ∈ {0,1} representa SURROUND y b ∈ {0,1} el estado del subwoofer. H es una matriz de transferencia dependiente de la frecuencia; no se conocen sus coeficientes sin medir o reconstruir el circuito completo.

Para estudiar el nivel de satélites se usa un modelo de hipótesis en dB:

G_sat(s,b) = G₀ · 10^((a·s + c·b + d·s·b)/20)

- a: cambio de nivel asociado a SURROUND.
- c: cambio asociado al subwoofer sin SURROUND.
- d: interacción entre ambos controles.
- G₀: nivel de referencia.

No se asignan valores numéricos a a, c ni d: no hay mediciones que los respalden.

Para señales eléctricas medidas en la misma carga, ΔL = 20·log₁₀(V₂/V₁). Para potencia con impedancia constante, ΔP = 10·log₁₀(P₂/P₁). Un teléfono grabando altavoces no permite inferir directamente potencia eléctrica porque incluye micrófono, AGC, habitación y suma acústica.

## Política de procesamiento de IVANNA (implementada)

- Ruta WIRED_AUX: bassBoostDb = 0 dB, dialogBoostDb = 0 dB, widenerMult = 1.0.
- Mantener L/R y polaridad; no crear satélites virtuales ni añadir surround antes del Sony.
- No aplicar compensación de ganancia inferida a partir de la escucha.
- El usuario conserva los botones físicos Stereo/Surround y subwoofer del amplificador.
- La ruta USB-C DAC sigue siendo independiente; este perfil AUX no modifica Bluetooth ni USB.

Esta política reduce el riesgo de duplicar el upmix, sobrecargar el subwoofer o alterar la imagen estéreo antes de la entrada del Genezi. Es un perfil conservador, no una calibración acústica medida.

## Protocolo de medición para identificar los coeficientes

Con volumen, señal, micrófono y posición fijos, medir cuatro estados:
A) Stereo + sub apagado; B) Stereo + sub encendido; C) Surround + sub apagado; D) Surround + sub encendido.

Usar barrido logarítmico a nivel moderado y medir por separado posición frontal y satélite. Para cada banda de frecuencia, comparar niveles RMS o respuesta impulsional. Estimar:
- c(f) = nivel(B) − nivel(A)
- a(f) = nivel(C) − nivel(A)
- d(f) = [nivel(D) − nivel(C)] − [nivel(B) − nivel(A)]

Estas diferencias son aproximaciones acústicas de la habitación; la estimación eléctrica requiere medición segura de señales de línea o documentación completa del circuito. Nunca conectar una salida amplificada de altavoz a un teléfono o entrada de línea.

## Criterio de aceptación

El perfil se mantiene neutro hasta que una medición reproducible demuestre la necesidad de corrección. Cualquier futura EQ debe limitarse a la ruta AUX, tener headroom y suavizado, y evitar boosts que se sumen al control tonal del Sony. No declarar respuesta en frecuencia, crossover, fase, ganancia de satélites ni mejora auditiva como hechos medidos antes de validar en el equipo real.
