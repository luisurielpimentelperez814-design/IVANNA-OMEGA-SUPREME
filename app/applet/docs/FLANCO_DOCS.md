# 📚 FLANCO DOCUMENTACIÓN DE PRODUCTO Y PRIVACIDAD

> 🔗 **Coordinación consolidada:** el índice maestro de todos los flancos
> es `AGENT_CLAIMS.md` en la raíz — revísalo también antes de reclamar o
> tocar cualquier área. Este archivo satélite se preserva por su detalle,
> pero puede estar desactualizado si no se edita en ambos lugares.


**Propietario del flanco:** agente Genspark — sesión iniciada 2026-09-08.
**Protocolo para cualquier otro agente (LEER ANTES DE TOCAR):**

> ⛔ **ESTE FLANCO ESTÁ TOMADO. NO MODIFICAR.** Cada agente trabaja UN
> SOLO flanco. Si necesitas un cambio aquí, deja una nota en AGENT_CLAIMS.md
> o aquí y lo integro yo. Escoge cualquier otro flanco (DSP, daemon/Magisk,
> UI/UX, conversación/Gemini, SAF-HRTF, IAEL, Web Dashboard, Tests host,
> Benchmarks, HRTF, supply-chain). No reviertas, no reformatees.

## Archivos bajo este flanco
- `PRODUCT_MASTER_STATUS.md` · `RELEASE_NOTES.md`
- `docs/PRIVACIDAD_Y_SEGURIDAD.md` · `docs/FLANCO_DOCS.md`
- (README/LÉAME se coordinarán antes de tocarlos; posible otra sesión)

## Hallazgos de raíz (auditoría 2026-09-08)
| Hallazgo | Severidad |
|---|---|
| Permisos muy sensibles sin documento de privacidad (declaración para tiendas) | Alta |
| `PRODUCT_MASTER_STATUS.md` vacío de contenido real | Media |
| `RELEASE_NOTES.md` sin notas de la sesión de agentes 2026-09-08 | Baja |

## Qué se construye (de raíz)
1. `docs/PRIVACIDAD_Y_SEGURIDAD.md` — permiso por permiso con propósito,
   qué sale del dispositivo, recomendaciones para Play/F-Droid.
2. `PRODUCT_MASTER_STATUS.md` — estado maestro real (build, tests, IAEL,
   releases) sincronizado con la evidencia de auditoría.
3. `RELEASE_NOTES.md` — entrada de la sesión de agentes 2026-09-08.

## Criterio de "terminado, world-class"
1. Cualquier usuario técnico puede responder "¿qué hace cada permiso?" con
   este documento y el código a la vista.
2. La declaración de Play puede derivarse directamente del documento.
3. El estado maestro refleja la realidad verificada (commits, PASS, CI).
