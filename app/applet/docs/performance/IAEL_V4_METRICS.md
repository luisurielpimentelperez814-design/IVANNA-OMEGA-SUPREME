# IAEL v4 — Referencias de definición de métricas

Documento de referencia de las métricas del laboratorio
`tools/iael_v4/iael_lab_engine.py`. Cada métrica tiene definición citable
para que el resultado sea falsable y comparable.

| Métrica | Definición | Referencia estándar |
|---|---|---|
| THD+N | (Potencia de armónicos 2..10 + residuo fuera de banda) / potencia del fundamental, en dB, con pico del fundamental por interpolación parabólica y banda de exclusión ~15 Hz | IEC 60268-3 (método de banda de rechazo) |
| SNR | Potencia del fundamental / potencia total fuera de su banda de exclusión (sin contar armónicos aparte), en dB | práctica ITU-R BS.1387 (sonoridad ruido vs señal) |
| IMD | Tono bajo 60 Hz (4x amplitud) + alto 7 kHz; laterales 7000±n·60 medidos en dB por debajo del tono alto | SMPTE RP-120 (4:1), también IEC 60268-3 mod. |
| Planitud espectral | Energy RMS por banda ISO de octava (31.5 Hz–16 kHz) vía FFT; desviación pp respecto de la media y banda peor | ISO 266 / IEC 61260 (bandas de octava) |
| Coherencia estéreo | Correlación de Pearson entre canales + error de fase espectral ponderado por magnitud en banda 100–4000 Hz | práctica de ingeniería de broadcast (ITU-R BS.1116 para subjetivo) |
| Transitorio | Overshoot (%) y settle time (ms) sobre escalón, tolerancia 1 dB | práctica de medición de atacantes/rampas |
| Validez | Conteo de NaN/Inf; conteo de muestras con |x|>=1.0 (clipping); pico | IEC 61672 (pico) + anti-NaN |
| Bit-exactness | Error máximo < 1e-9 entre entrada y salida del pipeline (bypass exacto) | criterio interno (identidad) |
| Latencia | Cross-correlación FFT entrada↔salida en modo captura WAV | práctica AC-3/AES (delay) |
| Throughput (stress) | Bloques procesados segundos de audio / tiempo real; >= 1.0x = tiempo real | criterio RT de audio |

## Tolerancias por defecto (TOLERANCES)
- THD+N <= -60 dB · SNR >= 60 dB · IMD <= -60 dB · planitud pp <= 6 dB
- Overshoot <= 5 % · latencia <= 10 ms · error bit <= 1e-9 · inválidos == 0
- Sobre pipeline identidad (modo self) el resultado es bit-exact y supera
  todos los límites (THD+N ~ -132 dB, IMD ~ -147 dB medidos).

## Reproducibilidad
Seed de fase fija (0xC0FFEE) en generadores; mismas señales → mismos números.
Modo `--mode wav` para medir un pipeline real (entrada/salida WAV).
