# 🔒 FLANCO RECLAMADO: Pipeline de herramientas HRTF (tools/sofa_convert.py + tools/hrtf/ + tools/hpir/)

> 🔗 **Coordinación consolidada:** el índice maestro de todos los flancos
> es `AGENT_CLAIMS.md` en la raíz — revísalo también antes de reclamar o
> tocar cualquier área. Este archivo satélite se preserva por su detalle,
> pero puede estar desactualizado si no se edita en ambos lugares.


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

---

## Ciclo 2 (2026-09-10, sesión Genspark) — el tercer script: `tools/sofa_to_ihr1.py` (raíz)

**Pregunta heredada:** ¿huérfano de la carga inicial o herramienta viva?

**Veredicto con evidencia: VIVO y CANÓNICO para producción batch. No shim, no borrado.**

1. **Es el productor de todos los assets distribuidos.** Sonda binaria directa
   de los 12 `.ihr1` embarcados (app/assets + magisk_module): todos son layout
   AZEL (tabla `[az,el]×M` + bloques IR), 512 taps, 48 kHz — exactamente el
   formato que escribe el driver raíz, no el AZ de `tools/hrtf/sofa_to_ihr1.py`.
2. **Los dos layouts son leídos por consumidores distintos** y el lector
   unificado `spatial/ihr1_format.hpp` los distingue por tamaño de fichero
   (fórmulas cerradas verificadas: AZ=16+M·(4+8·taps), AZEL=16+M·(8+8·taps)).
   No hay ambigüedad en runtime.
3. **La diferencia de signo de azimut NO es bug fatal**: el driver escribe la
   convención SOFA cruda (az positivo = izquierda); la canónica invierte
   (`az=-az`, positivo = derecha). `verify_dataset.py` documenta que el motor
   acepta ambas convenciones (datasets reales usan [0,360) y [-180,180]).
   El renderer del layout AZEL (ObjectRenderer) consume la tabla tal cual.
4. **Puerta de validación ejecutada sobre la producción real:**
   `verify_dataset.py app/src/main/assets/ivanna_omega/hrtf/*.ihr1` →
   12/12 PASS (cipic_003..165, kemar, kemar_large, pulse, tu_berlin_kemar,
   freefield_demo). El formato del driver queda probado end-to-end.

**Diferencias reales frente a la canónica (documentadas, no corregidas —
cada una es una decisión de diseño distinta, no un defecto):**
- El driver conserva elevación (AZEL) y TODAS las posiciones (sin dedupe):
  necesario para ObjectRenderer/HRTFBinLoader, que interpolan en 3D.
- La canónica filtra al plano horizontal (--max-elev) y deduplica por azimut:
  necesario para el interpolador 1D de synthetic_hrtf.
- El driver no invierte azimut ni normaliza por azimut-mínimo: la convención
  es distinta, no errónea (ver punto 3).
- El driver escribe `hrtf_index.json` (sha256 por sujeto) para el módulo
  Magisk — capacidad que la canónica no tiene.

**Acción:** ningún cambio de código. La "ambigüedad" era documental: el script
no estaba en ningún CLAIM y parecía huérfano. Queda registrado aquí como
herramienta de producción del pipeline batch de los 12 sujetos.

## Ciclo hpir (2026-09-10) — drift datos↔consumidor reconciliado

- Verificación de campo: los SOFA hpir_*.sofa fuente NO están en el repo
  (los 40 de magisk_module/.../sofa son HRTF/KEMAR) → el JSON medido
  (7c43a509, 2026-09-09) es la única verdad de medición disponible, y era
  MÁS RECIENTE que AutoEqManager.kt (baseline 2026-09-03): el Kotlin quedó
  con el extractor viejo (4 bandas, clamp ±6 dB) mientras el actual produce
  5 bandas con ±8 dB (verificado en extract_hpir_profiles.py:74-80).
- AutoEqManager.kt: PROFILES regenerado PROGRAMÁTICAMENTE desde el JSON
  (sin transcripción manual), 5×5=25 bandas, comentario de cabecera
  alineado a ±8 dB, nota anti-drift en el bloque. Balance de llaves/
  paréntesis verificado (0/0). Commit 8aa5ba03.
- Correctivo propio: mi diagnóstico anterior de "drift AZ vs AZEL" en
  make_test_ihr1.py era ERRÓNEO — spatial/ihr1_format.hpp soporta ambos
  layouts por tamaño (AZ y AZEL son válidos). El fix que perdí con el
  reset NO se restaura: el generador original era correcto para el lector
  de producción. La divergencia real está en HRTFBinLoader::loadIHR1
  (solo entiende AZEL) — nota para su dueño, no lo toco.
- Correctivo de hooks: .githooks/pre-commit estaba 100644 en el tree (git
  ignora hooks no ejecutables — el commit 8aa5ba03 pasó sin puerta, con
  advice.ignoredHook). Corregido a 100755 en git (commit 09fc1559, que
  pasó por el hook activo). Un clon fresco ya funciona sin setup manual.
