# IVANNA AUDIO VALIDATION REPORT
## GORE TNS · IVANNA-OMEGA-SUPREME

## Spotify — Ruta B omega_effect

- Reproducir pista conocida.
- Activar EQ.
- Activar Spatial modo 2.
- Activar NHO modo 1+.
- Confirmar:
  - rms > 0
  - adaptive_connected = 1
  - sin cortes.

PASS:
- THD < 0.5%
- CPU total <35%

---

## YouTube system-wide

Validar:
- AudioRoute source=omega_effect
- VoiceProtection activo con voz dominante.

PASS:
- Sin eco.
- Sin cortes.
- Latencia total <30ms.

---

## Bluetooth SBC/AAC

Validar compensación:
- bass_boost +3dB
- dialog_boost +2dB

PASS:
- Igualación perceptual contra cable.

---

## Audífonos cable

IvannaLab:

- THD <0.1%
- SNR >80dB
- LUFS objetivo -14 ±1

Latencia:

capture + DSP + output <25ms

CPU:

cpuTotalPercent <40%

---

## Targets

| Subsistema | CPU |
|-|-|
| DSP | <15% |
| Spatial | <10% |
| NHO | <8% |
| EvoKernel | <5% |
| Total | <35% |
