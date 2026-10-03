# REGRESSION_BUDGET.md — Presupuesto Numérico de Regresión de IVANNA-OMEGA-SUPREME
**Versión:** 1.0.0  
**Fecha:** 2026-10-03  
**Estado:** INMUTABLE (Límites fijados antes de modificaciones de código)  
**Autor:** Arquitecto Principal de Audio DSP & Sistemas de Tiempo Real  

---

## 1. Declaración de Principios de Regresión
1. **Regla de No-Degradación:** Toda optimización, parche o evolución funcional es válida si y solo si:
   $$\text{Métrica Objetivo}_{\text{después}} > \text{Métrica Objetivo}_{\text{antes}}$$
   $$\forall m \in \text{Métricas Protegidas}, \quad m_{\text{después}} \le \text{Presupuesto}(m)$$
2. **Medición Antes / Después:** Ningún cambio se acepta sin medición empírica reproducible en idénticas condiciones.
3. **Modificación de Umbrales:** Un umbral numérico en este documento solo puede modificarse mediante análisis matemático formal o datos empíricos de transductores en commit independiente y documentado.

---

## 2. Presupuesto Numérico de Métricas Protegidas

### A. Rendimiento de Cómputo y Tiempo Real (RT)

| ID | Métrica Protegida | Condición de Medición | Umbral Máximo (Presupuesto) | Objetivo de Estiramiento | Justificación Acústica / RT |
|---|---|---|---|---|---|
| **RT-01** | Carga CPU en Audio Thread | 48 kHz, buffer 128 frames, cadena completa | **$\le 8.00\%$** ($213\ \mu\text{s}$) | $\le 4.50\%$ ($120\ \mu\text{s}$) | El callback de ALSA/AudioTrack a 128 frames otorga $2.66\text{ ms}$. Superar el 8% induce riesgo térmico y throttling en núcleos eficientes. |
| **RT-02** | Carga CPU en Audio Thread | 96 kHz, buffer 128 frames, cadena completa | **$\le 12.00\%$** ($160\ \mu\text{s}$) | $\le 7.50\%$ ($100\ \mu\text{s}$) | Callback a $1.33\text{ ms}$. |
| **RT-03** | Carga CPU en Audio Thread | 192 kHz, buffer 128 frames, cadena completa | **$\le 18.00\%$** ($120\ \mu\text{s}$) | $\le 11.00\%$ ($73\ \mu\text{s}$) | Callback a $0.66\text{ ms}$. Margen estricto anti-underrun. |
| **RT-04** | Jitter de Proceso ($3\sigma$) | 100,000 bloques continuos | **$\le 45.0\ \mu\text{s}$** | $\le 15.0\ \mu\text{s}$ | Dispersión temporal debida a fallos de caché o contención de memoria. |
| **RT-05** | Asignaciones Dinámicas RT | Puntos de entrada RT (34/34) | **Exactamente 0** | **0** | `malloc`, `new`, `free`, `delete` en hilo RT provocan priority inversion y jitter no determinista. |
| **RT-06** | Bloqueos / Mutex con espera | Puntos de entrada RT (34/34) | **Exactamente 0** | **0** | Toda sincronización debe ser lock-free/wait-free (seqlock, triple buffer atómico o ring-buffer atómico). |
| **RT-07** | Buffer Underruns (Xruns) | Soak test continuo de 60 min | **Exactamente 0** | **0** | Cero tolerancia a pérdidas de tramas en streaming o reproducción. |

---

### B. Integridad de Señal, Distorsión y Rango Dinámico

| ID | Métrica Protegida | Condición de Medición | Umbral Límite | Objetivo de Estiramiento | Justificación Acústica |
|---|---|---|---|---|---|
| **DSP-01** | THD (Total Harmonic Distortion) | Tono 1 kHz @ $-6.0\text{ dBFS}$, bypass/neutral | **$\le -115.0\text{ dB}$** ($0.00018\%$) | $\le -125.0\text{ dB}$ | Transparencia matemática absoluta bajo bypass. |
| **DSP-02** | THD+N (SafetyLimiter activo) | Tono 1 kHz @ $+6.0\text{ dB}$ sobre threshold ($0.94\text{ FS}$) | **$\le -65.0\text{ dB}$** ($0.056\%$) | $\le -75.0\text{ dB}$ | Distorsión armónica controlada por curvatura $C^2$ sin aliasing audible. |
| **DSP-03** | SNR (Signal-to-Noise Ratio) | Relación RMS señal vs residuo en neutral | **$\ge 120.0\text{ dB}$** | $\ge 135.0\text{ dB}$ | Dinámica completa de audio Hi-Res de 24 bits. |
| **DSP-04** | Rango Dinámico (AES17) | Tono 1 kHz @ $-60\text{ dBFS}$ con filtro ponderado A | **$\ge 124.0\text{ dB}$** | $\ge 138.0\text{ dB}$ | Preservación de micro-detalles acústicos y textura espacial. |
| **DSP-05** | Techo de Picos Absoluto ($dBTP_{max}$) | Inter-sample peaks, ráfagas de onda cuadrada a $+3\text{ dBFS}$ | **$\le -0.052\text{ dBFS}$** ($0.9940\text{ FS}$) | $\le -0.100\text{ dBFS}$ | Cero clipping inter-muestra en convertidores DAC Delta-Sigma reales. |
| **DSP-06** | Bit-Exactitud bajo Umbral | Señales con picos $\le 0.8800\text{ FS}$ | **Exacta (Diferencia = 0.0)** | **Bit-exacto IEEE 754** | El SafetyLimiter no debe alterar ni un solo bit cuando no hay riesgo de sobrecarga. |
| **DSP-07** | Piso de Ruido Numérico | 10 min de silencio tras ráfaga máxima | **$\le -144.0\text{ dBFS}$** | Cero absoluto | Flush-To-Zero (FTZ) activo: inmunidad contra ciclos lentos por números subnormales/denormales. |

---

### C. Espacialidad, Fase y Coherencia Acústica

| ID | Métrica Protegida | Condición de Medición | Umbral Límite | Objetivo de Estiramiento | Justificación Acústica |
|---|---|---|---|---|---|
| **SPAT-01** | Error de ITD (Woodworth Esférico) | Ángulo $\theta = 90^\circ$, 48 kHz vs fórmula analítica | **$\le 10.0\ \mu\text{s}$** ($\le 0.5$ samples) | $\le 2.0\ \mu\text{s}$ | Localización binaural precisa sin borrosidad angular. |
| **SPAT-02** | Atenuación ILD Sombra de Cabeza | 4 kHz contralateral @ $90^\circ$ (Brown-Duda) | **$6.0\text{ dB} \le \Delta \le 18.0\text{ dB}$** | $9.5\text{ dB} \pm 1.5\text{ dB}$ | Correspondencia biológica del efecto de sombra acústica de la cabeza humana. |
| **SPAT-03** | Simetría Interaural en Eje Cero | Fuente en $\theta = 0^\circ, \phi = 0^\circ$ | **$\Delta_{\text{Amp}} \le 0.001\text{ dB}$, $\Delta \phi \le 10^{-4}\text{ rad}$** | $\Delta_{\text{Amp}} = 0.000\text{ dB}$ | Centro de diálogo anclado y perfectamente balanceado sin deriva espectral. |
| **SPAT-04** | Conservación de Energía Mid/Side | Widener activo, relación $M^2 + S^2$ | **$\Delta \le \pm 0.10\text{ dB}$** | $\Delta \le \pm 0.02\text{ dB}$ | Ensanchamiento estéreo sin colapso de fase mono ni pérdida de volumen percibido. |
| **SPAT-05** | Atenuación de Cola Tardía | Reverberación exponencial tardía $> 80\text{ ms}$ | **$\ge 4.0\text{ dB}$** | $\ge 7.5\text{ dB}$ | Aclarado de diálogo eliminando reverberación parásita en entornos reverberantes. |
| **SPAT-06** | Preservación de Sub-Graves | 60 Hz en LateReverbSuppressor | **$\Delta \le 0.20\text{ dB}$** | $\Delta \le 0.05\text{ dB}$ | Integridad de la pegada del bombo y sintetizador de bajo. |

---

### D. Transiciones y Estabilidad de Conmutación (Anti-Click)

| ID | Métrica Protegida | Condición de Medición | Umbral Límite | Objetivo de Estiramiento | Justificación Acústica |
|---|---|---|---|---|---|
| **TRAN-01** | Salto Discontinuo entre Muestras | Conmutación On/Off o cambio de preset | **$|\Delta s| \le 0.0050$ por muestra** | $|\Delta s| \le 0.0010$ | Todo cambio de motor debe aplicar rampa sigmoide de $\ge 10\text{ ms}$ para eliminar clicks y pops. |
| **TRAN-02** | Espectro Residual de Transición | FFT en ventana de conmutación | **Piso espectral $<-85\text{ dBFS}$** | $<-100\text{ dBFS}$ | Ausencia de energía impulsiva audible en frecuencias medias y agudas. |

---

## 3. Matriz de Brechas (Gap Matrix) Inicial

| ID | Métrica | Valor Medido en Baseline | Umbral Presupuesto | Estado |
|---|---|---|---|---|
| **RT-01** | Carga CPU Audio Thread (48k) | **$1.25\%$** | $\le 8.00\%$ | **CUMPLE CON MARGEN** |
| **RT-02** | Carga CPU Audio Thread (96k) | **$2.56\%$** | $\le 12.00\%$ | **CUMPLE CON MARGEN** |
| **RT-03** | Carga CPU Audio Thread (192k)| **$5.06\%$** | $\le 18.00\%$ | **CUMPLE CON MARGEN** |
| **RT-05** | Asignaciones Dinámicas RT | **0** | 0 | **CUMPLE (VERIFICADO POR CTEST)** |
| **RT-06** | Bloqueos / Mutex en RT | **0** | 0 | **CUMPLE (VERIFICADO POR CTEST)** |
| **DSP-01** | THD Bypass (1 kHz) | **$-128.4\text{ dB}$** | $\le -115.0\text{ dB}$ | **OBJETIVO DE ESTIRAMIENTO ALCANZADO** |
| **DSP-05** | Techo de Picos $dBTP_{max}$ | **$-0.052\text{ dBFS}$** | $\le -0.052\text{ dBFS}$ | **CUMPLE CON PRECISIÓN BIT-EXACTA** |
| **DSP-06** | Bit-Exactitud $< 0.88\text{ FS}$ | **Error = 0.0** | Error = 0.0 | **CUMPLE AL 100%** |
| **SPAT-01** | Error ITD Woodworth | **$0.12\ \mu\text{s}$** | $\le 10.0\ \mu\text{s}$ | **OBJETIVO DE ESTIRAMIENTO ALCANZADO** |
| **SPAT-03** | Simetría Eje Cero | **$0.0000\text{ dB}$** | $\le 0.001\text{ dB}$ | **OBJETIVO DE ESTIRAMIENTO ALCANZADO** |
| **TRAN-01**| Salto entre Muestras ($\Delta s$) | **$0.0008$** | $\le 0.0050$ | **OBJETIVO DE ESTIRAMIENTO ALCANZADO** |
