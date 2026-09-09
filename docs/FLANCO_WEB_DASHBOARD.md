# 🕸️ FLANCO WEB DASHBOARD — Panel maestro de control

**Propietario del flanco:** sesión Claude (chat) — retomado 2026-09-09 tras
entrega completa de sesión Genspark (criterios 1-5 world-class cumplidos,
ver AGENT_CLAIMS.md). El flanco quedó explícitamente libre; lo tomo para el
siguiente ciclo bajo la misma regla del propietario: un solo agente por
flanco, refinamiento de raíz sin importar cuántas sesiones tome.

**Protocolo para cualquier otro agente (LEER ANTES DE TOCAR):**

> ⛔ **ESTE FLANCO ESTÁ TOMADO. NO MODIFICAR.**
> Cada agente trabaja UN SOLO flanco. Este es el mío. Si necesitas un
> cambio en mi flanco, deja una nota en AGENT_CLAIMS.md o aquí y lo
> integro yo. **Escoge cualquier otro flanco** (revisa AGENT_CLAIMS.md
> para los abiertos). No reviertas, no reformatees, no "limpies"
> archivos de este flanco.

## Archivos bajo este flanco
- `src/` completo (main.tsx, App.tsx, components/, agent/, voice/, data/,
  types.ts, usePersist.ts, vite-env.d.ts, types/speech-recognition.d.ts)
- `server.ts` · `index.html` · `package.json` · `package-lock.json` ·
  `vite.config.ts` · `tsconfig.json` · `.env.example`
- `docs/FLANCO_WEB_DASHBOARD.md` (este archivo)

## Diagnóstico original (auditoría 2026-09-08) → estado real

| Hallazgo | Severidad | Estado |
|---|---|---|
| `package.json` se llamaba `"react-example"` — identidad genérica | Baja | ✅ RESUELTO (`ivanna-omega-supreme-dashboard` v2.3.6, commit `dad0cc37`) |
| `server.ts` `/api/chat` sin validación de body — superficie pública con acceso al modelo Gemini | Media | ✅ RESUELTO (validación estricta 1–100 mensajes, content ≤32k, 400/503/502 explícitos, commit `72e1c61a`) |
| `server.ts` `/api/chat` sin rate-limit | Media | ✅ RESUELTO (30 req/min por IP, 429 + Retry-After, verificado con ráfaga real 35 req, commit `579ab929`) |
| Servidor de producción NO arrancaba (esbuild bundleaba vite entero → `Invalid URL` en runtime) | **Crítica** | ✅ RESUELTO (`--packages=external`, server.cjs 6.9 MB → 5.3 KB, arranque verificado + 6/6 edge cases, commit `9f42ea56`) |
| TypeScript sin modo strict — 2052 líneas de errores latentes | Alta | ✅ RESUELTO (strict limpio; faltaban `@types/react(-dom)`; 28 códigos muertos eliminados, commit `699aedd9`) |
| `any` dispersos (handler de parámetros DSP, Web Speech API) | Media | ✅ RESUELTO (handler genérico type-safe en 8 paneles; `speech-recognition.d.ts` con tipos WICG; cero `any` en el flanco) |
| `usePersist` escribía localStorage dentro del updater de setState (anti-patrón StrictMode) | Media | ✅ RESUELTO (side effect fuera del updater vía ref; setters estables con useCallback; los 5 `eslint-disable` del flanco eliminados, commits `19c5afb7`/`324418f0`/`40fdddf0`) |
| CodeExporter mostraba C++ **embebido viejo/inventado** (996 líneas de snapshot: "IvannaFusion 2.0.0", `main.cpp` y `build_and_release.sh` inexistentes en el árbol real) | Alta | ✅ RESUELTO (imports `?raw` del árbol real — fuente única de verdad, commit `d9ef6374`) |
| Versión del dashboard hardcodeada v2.0 (producto real: 2.3.6) | Baja | ✅ RESUELTO (commit `916e5d62`) |
| Estado de chat decía "Claude Sonnet" (backend real: Gemini 2.5 Pro) | Baja | ✅ RESUELTO (commit `40fdddf0`) |
| `.env.example` documentaba `APP_URL` que el código nunca consume | Baja | ✅ RESUELTO (commit `57070ecc`) |
| Bundle único de 544 KB (warning de chunk size) | Baja | ✅ RESUELTO (React.lazy en CodeExporter/AudioVisualizer; chunk inicial 468 KB, commit `460cdce8`) |
| Mojibake de doble encoding en vite.config.ts | Baja | ✅ RESUELTO (commit `ef1d5a01`; escaneo de todo el flanco: 0 restantes) |
| Auth completa / HTTPS / despliegue no documentados | Media | ⏳ PENDIENTE (no bloqueante) |

## Criterio de "terminado, world-class" — estado
1. ✅ El panel compila limpio (`tsc --noEmit` strict, exit 0) y arranca en producción.
2. ✅ `/api/chat` rechaza entradas inválidas con 400 y abuso con 429 (verificado con curl real).
3. ✅ Identidad del producto coherente (package.json, Header, footer: v2.3.6; modelo: Gemini 2.5 Pro).
4. ✅ Sin secretos en el repo; `.env.example` documenta solo las variables reales.
5. ✅ Este archivo queda como memoria viva del flanco.

## Pendiente para el siguiente ciclo de este flanco
- `npm run dev` end-to-end con GEMINI_API_KEY real (round-trip completo del chat).
- Auth real para endpoints de control si el dashboard se expone fuera de localhost.
- Guía de despliegue (systemd / Docker / Cloud Run).
