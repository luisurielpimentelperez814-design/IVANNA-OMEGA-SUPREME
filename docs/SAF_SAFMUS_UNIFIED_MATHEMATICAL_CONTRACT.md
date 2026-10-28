# Contrato matemático unificado SAF / SAFMUS

**Estado:** corrección software acotada en la ruta propietaria; la corrección de hardware requiere evidencia independiente.

## 1. Dominio y convención de signos

Esta implementación controla únicamente el AudioTrack que crea IvannaBridgePlayer.

- Fs: frecuencia nominal del AudioTrack, en frames/s.
- Nw: frames acumulados aceptados por el mismo AudioTrack.
- Np: posición de frames presentada reportada por AudioTrack.getTimestamp().
- tp: AudioTimestamp.nanoTime, monotónico y pareado con Np.
- B = Nw − Np: frames en cola estimados.
- B* = max(1, floor(C/2)): ocupación objetivo para capacidad C.
- e = clip((B − B*)/C, −1, 1): error normalizado de ocupación.

Convención del actuador Kotlin: corrección ppm positiva reduce el avance de muestras de entrada por frame de salida y aumenta la producción de frames de salida. Un reloj consumidor más rápido que el nominal requiere corrección positiva.

## 2. Estimación de deriva

Para pares (ti, Np,i), con tiempo y frames centrados respecto al primer punto:

F_hat = sum((ti − mean(t))(Np,i − mean(Np))) / sum((ti − mean(t))^2)

y_hat_ppm = 10^6 (F_hat/Fs − 1).

La estimación se acepta solo si timestamps y frames avanzan, el intervalo está dentro del horizonte permitido, la tasa es finita y la deriva permanece dentro del límite. Ante timestamps ausentes, regresión, cambio de ruta o cola imposible, el controlador se reinicia y el actuador vuelve a unidad.

## 3. Planta de búfer y control

B_k = Nw,k − Np,k,    e_k = clip((B_k − B*)/C, −1, 1).

Banda muerta continua de anchura normalizada d = 0.02:

e_d = 0 si |e| ≤ d; e_d = e − d si e > d; e_d = e + d si e < −d.

Integrador acotado:

I* = clip(I_prev + e_d Δt, −I_max, I_max).

El anti-windup condicional rechaza la actualización integral cuando la orden sin saturar ya excede el límite y el error la empujaría aún más hacia afuera. El término de ocupación y el feed-forward de deriva comparten un único actuador:

u = clip(y_hat_ppm − Kp e_d − Ki I, −500, 500).

La salida cambia como máximo 25 ppm por actualización. El resampler recibe u mediante StereoAudioResampler.setClockCorrectionPpm(u) y conserva la fase fraccional entre bloques.

## 4. Invariantes de seguridad

1. Un único controlador escribe la corrección de la instancia activa de StereoAudioResampler.
2. El callback de audio no realiza I/O de reloj ni comparte el control con el hilo del daemon.
3. La corrección pertenece a [−500, 500] ppm y cambia como máximo 25 ppm por actualización.
4. El error dentro de la banda muerta no acumula integral.
5. Timestamp inválido, discontinuidad de ruta o medición imposible implica corrección cero y reinicio del estimador.
6. Las métricas HAL/PAL compartidas no se inyectan en el PLL de AudioTrack: no se ha demostrado que ambos representen el mismo stream.
7. Esta ruta no controla el oscilador remoto Bluetooth ni certifica la tasa física del DAC.

## 5. Pruebas y evidencia requerida

Regresiones JVM: signo del feed-forward, estimación OLS, corrección acotada, reinicio ante timestamps regresivos, ruido dentro de la banda muerta y saturación sostenida. CI debe ejecutar también el build Android y las comprobaciones de artefactos.

La validación en hardware debe medir underruns, clicks, continuidad de frames, latencia y deriva residual durante reproducción prolongada, incluidas transiciones USB/Bluetooth. La telemetría y el éxito de CI no sustituyen estas medidas.
