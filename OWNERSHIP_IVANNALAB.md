# FLANCO IvannaLab — ENTREGADO PARCIAL (pendiente coordinacion con Tests host)
**Sesion Genspark, 2026-09-08.** UN flanco por agente; NO tocar salvo coordinando con el flanco Tests host.

## Entregado y VERIFICADO
1. K-weighting BS.1770-4 disenado para la sample rate real (antes 48 kHz fijo a 96 kHz — shelf doblado, LUFS sesgado).
2. LUFS integrada BS.1770-4 completa: gate absoluto (-70 LUFS) + relativo (-20 LU) — antes snapshot sin gate relativo.
3. SNR real (p10/p90 de energias de bloques 100 ms como ventana de silencio estadistica) — antes RMS global en dBFS (decorativo, documentado como falso en el repo).
4. LRA BS.1770-4 Annex 2 (p95-p10 con gates).
5. True Peak FIR 4x BS.1770-1; THD DFT Hann H2/H3/H4; IMD SMPTE 250Hz/8kHz — auditados OK.
6. Carreras de lectura eliminadas: framesAccumulated()/hasEnoughData() con mutex (tombstone SIGSEGV documentado en la clase).
7. Compilacion host de ivannalab.cpp: verificada (g++ -fsyntax-only, exit 0). Puerta global: 67/67.

## NO CONFIRMADO (pendiente de coordinacion)
- test_ivannalab.cpp (7 tests con senales sinteticas: THD 1.118%, IMD SMPTE 2.0%, SNR 56.99 dB, LUFS -23.7, LRA 12 LU, peak/true peak, estado vacio) fue CORREGIDO (12 lambdas -> float) pero NO enganchado en CI: engancharlo exige anadir ivannalab.cpp al target ivanna_dsp_under_test del flanco Tests host (otro agente, cerrado). NO ejecutado — marcado unconfirmed.
