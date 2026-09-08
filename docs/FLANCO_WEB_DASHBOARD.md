# 🕸️ FLANCO WEB DASHBOARD — Panel maestro de control

**Propietario del flanco:** agente Genspark — sesión iniciada 2026-09-08.
**Protocolo para cualquier otro agente (LEER ANTES DE TOCAR):**

> ⛔ **ESTE FLANCO ESTÁ TOMADO. NO MODIFICAR.**
> Cada agente trabaja UN SOLO flanco. Este es el mío. Si necesitas un
> cambio en mi flanco, deja una nota en AGENT_CLAIMS.md o aquí y lo
> integro yo. **Escoge cualquier otro flanco** (DSP nativo, daemon/Magisk,
> UI/UX Compose, Conversación/Gemini, SAF-HRTF, IAEL — revisa
> AGENT_CLAIMS.md para los abiertos). No reviertas, no reformatees, no
> "limpies" archivos de este flanco.

## Archivos bajo este flanco
- `src/` (App.tsx, components/ 14, agent/, voice/, data/, types.ts, usePersist.ts)
- `server.ts` · `index.html` · `package.json` · `vite.config.ts` · `tsconfig.json` · `.env.example`
- `docs/FLANCO_WEB_DASHBOARD.md`

## Diagnóstico de raíz (auditoría 2026-09-08)
| Hallazgo | Severidad |
|---|---|
| `package.json` se llama `"react-example"` — identidad genérica | Baja |
| `server.ts` `/api/chat` sin validación de body ni rate-limit — superficie pública con acceso al modelo Gemini | Media |
| Auth completa / HTTPS / despliegue no documentados | Media |

## Qué se está construyendo (de raíz)
1. Identidad correcta del paquete (`ivanna-omega-dashboard`).
2. Seguridad básica del endpoint: rate-limit por IP en memoria (30 req/min), validación de `messages` (1..40, content <= 4000) y `system` (<= 8000).
3. Typecheck (`tsc --noEmit`) verificado cuando el entorno lo permite.
4. Pendiente (no bloqueante): auth real para endpoints de control, lockfile dedicado, guía de despliegue.

## Criterio de "terminado, world-class"
1. El panel compila limpio y arranca.
2. Endpoint `/api/chat` rechaza entradas inválidas con 400 y abuso con 429.
3. Identidad del producto coherente en package.json/index.html.
4. Sin secretos en el repo; `.env.example` documentado.
5. La doc del flanco queda como memoria viva (este archivo).
