# Propuesta 0001 — SAF_GUARD para `PhiSAFInfinity`

- **Motor:** `tools/evolution/evolution_engine.py` (`--scan` → detector
  `saf_without_guards` + `dead_functions`)
- **Fecha:** 2026-11-09
- **Estado:** VALIDADA (tests nuevos 6/6 + suite completa 848/848). Lista para
  revisión de Esse. NO aplicada al working tree (el entregable es este diff).
- **Archivos:** `0001-saf-phi-infinity-guard.patch` (202 líneas, 3 archivos,
  +156/−5), `validation.json` (validación automatizada del motor).

## Hallazgo (real, medido, no inventado)

`PhiSAFInfinity` (`app/src/main/cpp/saf_phi_infinity.cpp`, 35 líneas, pura C++,
compilada en el APK según `app/src/main/cpp/CMakeLists.txt:78,440`) es la
única ruta SAF sin las guardas del patrón **SAF_GUARD (2026-09-17)** que su
función hermana `SAFUpdate` (`app/src/main/cpp/include/saf_runtime.h`) sí
tiene. Además hoy no tiene llamadores (código muerto compilado en el binario)
y no tenía test.

**Repro medido antes del fix** (`g++ -std=c++17`, `/tmp/repro_saf.cpp`,
misma función, sin tocar el repo):

| Entrada | Gain antes | Gain después del fix |
|---|---|---|
| normal (dE=0.1, d=0.1, G=1) | 1.09091 | 1.09091 (bit-idéntica) |
| G=0 | **2.0 (máximo)** | 1.0 (sin cambio) |
| G=−5 | **2.0** | 1.0 |
| G=NaN | **2.0** | 1.0 |
| deltaE=NaN | **2.0** | 1.0 |
| delta=NaN | **2.0** | 1.0 |
| memory=NaN | **2.0** | 1.0 |
| delta=+Inf | **2.0** | 1.0 |

Una sola entrada degenerada clavaba el gain en el máximo. Causas: (1) sin
guarda NaN/Inf; (2) la sanitización de G ocurría DESPUÉS de usarla en
`norm`/`alpha`; (3) con G en el piso 1e-6 el update es `alpha·delta·1e6`,
numéricamente sin sentido.

## Fix (sigue el patrón SAF_GUARD ya establecido en el codebase)

1. Guarda NaN/Inf en las 4 entradas → fail-closed, devuelve el gain actual
   sin tocar el estado.
2. G degenerada (≤1e-6, seis órdenes bajo el nominal `Gt=1.0`) → paso cero.
3. Sanitización de G movida ANTES de su uso (antes: después).
4. Denominador ≤0 o no finito → paso cero seguro (mismo criterio de SAFUpdate).
5. Guarda de `update` no finito.

La ruta válida es bit-idéntica (1.09091 antes y después): cero cambio de
semántica en entradas sanas.

## Test nuevo

`app/src/main/cpp/tests/test_saf_phi_infinity.cpp` (6 tests, gtest, enlaza
contra el `saf_phi_infinity.cpp` REAL): ruta normal + silencio identidad +
G degenerada + 9 entradas no-finitas + denominador degenerado + clamp [0,2].
Registrado en `tests/CMakeLists.txt` con el patrón `add_executable`
(explícito, como `test_evolutionary_eq`).

## Validación

- Tests nuevos: **6/6 PASSED**.
- Suite host completa: **848/848 PASSED, 0 fallos** (baseline 847/847; +1 es
  este test nuevo). Total ~129 s.
- Validación automatizada del motor: `validation.json` (ciclo
  aplicar → compilar → tests nuevos → suite completa → revertir).
- El ciclo del motor se corrió 3 veces; en las 2 primeras se encontraron y
  corrigieron 2 bugs del propio motor (no del parche): (1) el parseo de
  `+++ b/<path>` dejaba el prefijo `b/` y el revert no tocaba nada;
  (2) `git checkout -- <paths>` falla ENTERO si un pathspec es un archivo
  nuevo (untracked), así que el revert ahora separa tracked (checkout) de
  nuevos (unlink) y VERIFICA que el árbol quedó limpio (`reverted` es un
  booleano medido, no asumido). La tercera corrida es verde de punta a
  punta con el árbol verificado limpio.

## Riesgo

Mínimo. Función sin llamadores actuales; el cambio solo añade guardas
fail-closed y la ruta válida es bit-idéntica. Si un llamador futuro la
cablea (p. ej. el optimizador SAF), hereda las guardas en vez del bug.
