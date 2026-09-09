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

---

## CIERRE DEL RELEVO (2026-09-09, sesión Genspark) — flanco ENTREGADO

Los 4 puntos de la evidencia rota, cerrados con commits verificables:

1. **Asset corrupto → RESTAURADO byte-perfecto** (`73812665`). Forense nuevo
   sobre la evidencia: el archivo no tenía "otro layout" — estaba mutilado por
   una pasada de codec UTF-8 (lleno de U+FFFD `ef bf bd`; cada byte inválido
   reescrito como 3 bytes con pérdida irreversible). Commit corruptor:
   `bde62755` (único que lo tocó tras su creación). Restauración = checkout
   de `996a6259` (creador): verificado byte a byte (2,908,184 B =
   24 + 710×2×512×4 exacto, sr=44100, datos float sanos, cero U+FFFD).
   NO se regeneró con herramientas: habría producido un dataset distinto.
   Notificación formal al flanco DSP/SAF en AGENT_CLAIMS.md: `loadIVHRTF01()`
   hace resize() sin validación de rango (OOM demostrado como posible);
   `ihr1_format.hpp` sí acota — el fix del lector es suyo, no mío.

2. **`sofa_convert.py` → shim seguro** (`97faa76c`). Ya no escribe binarios:
   redirige a `tools/hrtf/sofa_to_ihr1.py` conservando args y exit code; sin
   argumentos muestra la ayuda y sale con código 2 (ningún CI heredado puede
   fingir éxito). Verificado por ejecución real. Grep verificado: ningún
   workflow/script/doc lo invocaba como herramienta de generación.

3. **Puerta de validación creada** (`5eb5fe22`): `tools/hrtf/verify_dataset.py`
   — replica las reglas de AMBOS lectores C++ (IHR1 AZ/AZEL por tamaño exacto,
   IVHRTF01 legacy; rangos acotados aunque el lector legacy no acote; datos sin
   NaN/Inf, energía no nula, |x|≤4; ángulos en dominio físico — NO una
   convención: primera versión rechazó los .ihr1 del módulo por reglas
   inventadas, la lectura directa de los archivos corrigió la regla).
   Verificada: 13/13 datasets del repo PASS + casos negativos sintéticos FAIL
   con causa nombrada (réplica exacta del corrupto real incluida).

4. **Pipeline end-to-end verificado por primera vez**: SOFA KEMAR real (44.1k)
   → `sofa_to_ihr1.py` (resampleo polifásico a 48k, dedupe por azimut) →
   72 direcciones → `verify_dataset.py` PASS. La cadena completa que nunca se
   había verificado, verificada.

5. **Bonus HpIR** (`4d6d0d6d`, `7c43a509`): `extract_hpir_profiles.py` deja de
   depender de rutas absolutas de una máquina (argparse + defaults relativos;
   ya no escribe `[]` con éxito aparente si falta toda la entrada) y excluye
   la región del shelf de la detección de bandas — los 5 perfiles llevaban una
   banda artefacto idéntica a 23.4 Hz +8 dB solapando con el shelf (doble
   compensación de graves; el JSON publicado la curaba a mano). JSON
   regenerado reproducible. `AutoEqManager` (perfiles embebidos) NO tocado:
   territorio del flanco espacial.

6. **Documentación alineada** (`8437dc1c`): `docs/HRTF_DATASET.md` — nueva §6
   (formato IVHRTF01 legacy documentado por primera vez + lección UTF-8),
   divergencia de límites entre los dos lectores IHR1 documentada (1024 vs
   8192), y §7: la puerta de validación como paso obligatorio.

**Queda libre.** Pendiente no bloqueante para quien lo retome: enganchar
`verify_dataset.py` al CI (territorio del flanco Tests host — notificado en
AGENT_CLAIMS.md) y regenerar el dataset embarcado del módulo si algún día se
quiere uno distinto del actual.
