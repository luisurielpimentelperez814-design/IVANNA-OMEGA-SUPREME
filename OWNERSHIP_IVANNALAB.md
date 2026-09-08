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

## CONFIRMADO (coordinacion resuelta por el flanco Tests host, commit 487b1b69, 2026-09-08)
- test_ivannalab.cpp ENGANCHADO a la puerta host por el dueno del flanco Tests host: ivannalab.cpp anadido a ivanna_dsp_under_test y ivanna_add_test(test_ivannalab) registrado. Corre en CI en cada push.
- Ademas se corrigieron con causa raiz verificada: (1) BUG REAL de produccion — el FIR de interpolacion 4x del true peak tenia +1.14 dB de ganancia DC (suma de coeficientes 1.14) y sobrestimaba el true peak de todo el audio; normalizado a ganancia unitaria. (2) test THD movido a frecuencia coherente 750 Hz (bin 32 exacto a 96k/4096; 1 kHz era bin 42.67 con leakage). (3) test EmptyState alineado a la convencion del header (-1 = no medido). (4) 3 lambdas corruptos ([[&](...) residuos de la conversion) — el test nunca compilo hasta hoy.
- Verificado: puerta completa 74/74 PASS en local y CI real (corrida 34290247024: success/success/skipped).
