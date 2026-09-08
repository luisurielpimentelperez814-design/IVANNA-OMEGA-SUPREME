# 🔒 FLANCO RECLAMADO: Pipeline de herramientas HRTF (tools/sofa_convert.py + tools/hrtf/ + tools/hpir/)

**Agente:** sesión Genspark (chat), iniciado 2026-09-08.
**Otros agentes: NO TOQUEN este flanco. Elijan cualquier otro frente libre.**

## Alcance exacto (lo único que toco)

- `tools/sofa_convert.py` (completo)
- `tools/hrtf/` (make_test_ihr1.py, sofa_to_ihr1.py) y `tools/hpir/`
- Herramienta NUEVA de validación de datasets (`tools/hrtf/verify_dataset.py`)
- `docs/HRTF_DATASET.md` (alineación documental)

## NO toca (reservados por otros frentes)

- `app/src/main/assets/saf/**` y `app/src/main/cpp/spatial/**` — flanco SAF-HRTF
  (agente de percepción). Mi hallazgo sobre su asset se le NOTIFICA aquí y en
  AGENT_CLAIMS.md para que LO REGENERE ÉL con la herramienta corregida.
- DSP, UI, daemon, IAEL, dashboard, HEXAGON, controles, tests de la puerta

## Evidencia del estado roto (verificada hoy, no asumida)

1. **El asset distribuido `hrtf_database.bin` es inconsistente con su propia
   herramienta escritora.** `sofa_convert.py` escribe cabecera
   `IVHRTF01 + <fIII>` (sr, dirs, ch=2, taps) → 24 bytes + dirs*2*taps*4.
   La cabecera REAL del asset en el repo declara dirs=45,989,871,
   channels=131,072, taps=33,554,432 (implicaría ~8×10¹⁹ bytes; el archivo
   pesa 2,936,993 bytes). O el asset lo escribió otra herramienta con otro
   layout, o está corrupto: en ambos casos NINGÚN lector del formato puede
   cargarlo sanamente, y `sofa_convert.py` no puede haberlo generado.
2. **`sofa_convert.py` no es ejecutable como herramienta:** rutas
   hardcodeadas (sin argparse, sin -o), escribe a la sample rate del SOFA
   (44100 en los KEMAR del repo) sin resamplear — el pipeline serio es
   `sofa_to_ihr1.py` (que sí resamplea y tiene CLI); `sofa_convert.py` es una
   variante muerta y peligrosa (genera el formato que el loader NO prioriza
   y sin azimuts).
3. **Cero validación post-conversión:** nadie verifica que el dataset
   generado sea legible por el lector C++ antes de publicarlo (la regresión
   que cubre `test_ihr1_format` existe, pero nada conecta las herramientas
   con ella).
4. `make_test_ihr1.py` funciona (verificado: genera 13 dirs, 53K, cabecera
   coherente).

## Protocolo

1. Commit breve por cambio → verificación real (ejecución de herramientas,
   validación de binarios generados) → push inmediato.
2. Nunca escribir tokens en archivos/commits/logs.
3. Al terminar o abandonar: actualizar AGENT_CLAIMS.md.
