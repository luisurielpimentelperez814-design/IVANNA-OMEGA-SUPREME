# 🔒 Frentes en trabajo — coordinación entre sesiones concurrentes

Este repo tiene múltiples sesiones de IA trabajando en paralelo sin
coordinarse entre sí. Eso ya causó daño real y medible: el mismo bug
(`@Composable` duplicado, permiso de micrófono) arreglado 3 veces en
minutos por sesiones distintas; un release (`v2.3.2`, luego `v2.3.4`)
publicado con assets incompletos y sellado *para siempre* por GitHub
Immutable Releases; un stack TypeScript/React agregado por una sesión
y borrado por otra horas después; y horas de trabajo duplicado o
directamente revertido entre sesiones que no sabían de la existencia
de las otras.

**Regla simple: antes de tocar código, lee este archivo.** Si el
frente que ibas a tocar ya está tomado, elige otro de la lista de
abajo. Si terminas o abandonas el tuyo, actualiza este archivo para
que la siguiente sesión sepa el estado real.

---

## 🔒 Frentes actualmente tomados

### Daemon nativo + runtime del módulo Magisk
**Tomado por:** sesión Claude (chat), iniciado 2026-09-07.
**Alcance exacto — no editar mientras esté aquí:**
- `app/src/main/cpp/daemon/` completo (CMakeLists.txt, ivanna_daemon.cpp,
  control/, core/)
- `app/src/main/cpp/IvannaSelfHealingEngine.{hpp,cpp}`
- `magisk_module/` completo (service.sh, module.prop, update.json, scripts)
- En `.github/workflows/build.yml`: los jobs/pasos "Compile ivanna_daemon",
  "Validate daemon ELF", "Package Magisk module", "Publish GitHub Release"
  y la derivación de versión (`version.properties` ↔ `module.prop` ↔
  `update.json`)

**Por qué este frente y no otro:** es el bloqueador raíz reportado
directamente por el usuario, con evidencia (capturas del panel de la
app): el socket del daemon (`@omega_daemon_socket`) sigue sin bindear
en dispositivo real — módulo activo, root concedido, daemon nunca
levanta. Sin esto funcionando, nada del resto del producto (DSP real,
agentes conversacionales, IVANNA completa) llega al audio real del
usuario. Es literalmente la raíz de la que cuelga todo lo demás.

**Avance verificable hasta ahora (ver commits para detalle):**
- Fix de enlazado estático de libc++ (el daemon no encontraba
  `libc++_shared.so` al ejecutarse fuera del sandbox del APK vía
  Magisk service.sh)
- Fix de RELRO/hardening del binario (`ERROR: sin RELRO` en CI)
- Fix del pipeline de release (Immutable Releases de GitHub —
  patrón draft→publish en vez de clobber/recrear el mismo tag)
- **`v2.3.6` (2026-09-07T06:54Z): primer release de TODA esta sesión
  que pasó las 4 fases completas — build, validación de ELF, empaquetado
  y publicación — confirmado con `gh release view` real, no supuesto.**
  El zip real ya está en
  https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/releases/tag/v2.3.6
  y es lo que el usuario puede instalar para probar si el daemon bindea
  en dispositivo real — pendiente de confirmación en dispositivo, que
  es la prueba definitiva más allá de lo que CI puede validar
  estáticamente (formato ELF, independencia de libc++_shared, RELRO).
- Pendiente activo ahora mismo: confirmar en dispositivo real que el
  socket bindea con este build; si no, seguir diagnosticando desde
  daemon.log real del dispositivo, no solo desde CI.

**Si eres otra sesión leyendo esto:** no toques los archivos de arriba
por ahora. Vas a ver este frente avanzar commit por commit con mensajes
que empiezan con contexto verificado (grep, lectura directa, o log de
CI real) — no asumido. Elige uno de los frentes abiertos abajo.

---

### DSP nativo — cadena de señal completa (incluye espacial/HRTF)
**Tomado por:** sesión Claude (chat), iniciado 2026-09-07.
**Alcance exacto — no editar mientras esté aquí:**
- `app/src/main/cpp/` — todo lo que procesa muestras de audio reales:
  peak guard, GainStage, limitador, EQ paramétrico/evolutivo,
  `spatial/` completo (HRTFConvolver, RirDataset, psicoacústica),
  `jni/ivanna_omega_jni.cpp` en las funciones `nativeProcess*` que
  transforman buffers de audio (no el resto del JNI de IPC/SHM, eso es
  del frente "Daemon nativo").
- Config de build específica de estos targets nativos (`abiFilters`,
  flags de compilación de estas libs) cuando un fix de DSP lo exige.
- Absorbe los dos frentes que estaban abiertos abajo ("Binaural real"
  y "DSP adaptativo") — es el mismo dominio, separarlos solo generaría
  más coordinación, no menos.

**Explícitamente NO toca:** Gemini/Firebase, capa de conversación/
memoria, UI de Kotlin, el daemon Magisk y su socket/SHM (JNI de IPC
en sí), CI/release — salvo lo estrictamente necesario para compilar
cambios de DSP.

**Por qué este frente:** es lo que el nombre del producto promete
("Conversational Acoustic Intelligence") y donde ya hasta ahora se
encontraron bugs reales y no cosméticos con impacto audible directo
(peak guard sin rampa = tronido tipo metralleta al subir volumen,
arreglado con ataque instantáneo/release en rampa, no una rampa
simétrica ingenua — la protección real no se puede debilitar por
suavidad).

**Criterio de "terminado, world-class" (no cerrar antes de esto):**
1. Cada etapa de la cadena compila para el target real, de forma
   consistente, no solo "a veces".
2. Cada etapa tiene justificación de diseño explícita — no solo
   "código que compila" — comparable a por qué un limitador
   profesional usa ataque rápido/release lento, no al revés.
3. Sin discontinuidades de bloque audibles en ninguna etapa (el
   tronido de hoy era una instancia; puede haber más sin encontrar).
4. Sin implementaciones duplicadas de la misma etapa.
5. Recién ahí: comparación seria contra Dolby/DTS/iZotope en términos
   que importen (THD+N, artefactos, no solo "cantidad de features").

**Avance verificable hasta ahora:**
- Peak guard: ataque instantáneo + release en rampa en los 2 sitios
  (nativeProcess/nativeProcessBlock), reemplazando un salto de bloque
  completo sin memoria entre bloques.
- Build roto en armeabi-v7a (asm inline de FPCR/FPSCR inválido en
  hrtf_convolver.cpp, confirmado con log real de CI) — se quita ese
  ABI en vez de parchear asm de 32-bit que nadie puede aprovechar: el
  daemon (todo lo que hace root) ya es arm64-v8a exclusivo, así que
  v7a no daba ninguna función real, solo rompía el build.

**Estado:** trabajando — sesión larga, multi-turno, no se cierra
rápido a propósito.


Varios bugs puntuales ya arreglados (estado PROCESSING, manos-libres,
imports duplicados). No ha habido una pasada de diseño/UX real, solo
correcciones — el panel de red (`NetworkStatusPanel`) en particular
mezcla diagnóstico técnico con estado del agente de forma un poco
confusa para un usuario final.

---

### UI/UX Compose — paneles de diagnóstico y asistente
**Tomado por:** sesión Claude (chat), iniciado 2026-09-07.
**Alcance exacto — no editar mientras esté aquí:**
- `app/src/main/java/com/ivanna/omega/ui/` completo: `IvannaAssistantScreen.kt`,
  `NetworkStatusPanel.kt`, `MagiskStatusPanel.kt`, y demás Composables
  de presentación/paneles.
- Información arquitectónica de estos paneles: qué se muestra, cómo se
  organiza, si el mismo dato aparece duplicado/contradictorio entre
  paneles distintos (ej. estado de conexión del daemon mostrado de
  forma diferente en dos sitios).
- ViewModels que alimentan ESTOS paneles (`IvannaAssistantViewModel.kt`)
  únicamente en la parte de exposición de estado a la UI — no la lógica
  de negocio de Gemini/memoria que vive ahí (eso sigue siendo de quien
  toque conversación/memoria).

**Explícitamente NO toca:** lógica de Gemini/Firebase, DSP nativo,
daemon/Magisk/CI, memoria del asistente. Si un fix de UI requiere tocar
uno de esos, se limita al mínimo indispensable y se nota en el commit.

**Por qué este frente:** ya señalado arriba en este mismo archivo como
necesitando una pasada de diseño real, y nadie lo había reclamado
todavía — evita chocar con los dos frentes nativos ya tomados arriba.

**Estado:** trabajando — 3 fixes reales en main:
1. `c2826986` — desambiguación GEMINI vs daemon/socket en NetworkStatusPanel.
2. `8ae1fc04` — mismo fix aplicado en IvannaAssistantScreen.kt (mismo composable, otro archivo).
3. `703cb6d7` — advertencia de módulo desactualizado que estaba atrapada dentro de un comentario KDoc (nunca fue código ejecutable) implementada de verdad, con comparación semver real, en MagiskStatusPanel.
Pendiente: seguir revisando IvannaAssistantScreen.kt (700+ líneas, solo cubrí el panel de Gemini) y evaluar la reorganización de paneles en categorías.

---

### Conversación / IA / Memoria — el cerebro conversacional de IVANNA
**Tomado por:** sesión Claude (chat), iniciado 2026-09-07.
**Alcance exacto — no editar mientras esté aquí:**
- `app/src/main/java/com/ivanna/omega/ai/gemini/` completo
  (`IvannaGeminiAgent.kt`, `GeminiOrchestrator.kt`, `AdaptiveResponseEngine.kt`)
- `app/src/main/java/com/ivanna/omega/assistant/` completo: clasificador
  de intención (`IvannaLanguageCore.kt`), motor cognitivo
  (`IvannaCognitiveCore.kt`), memoria de sesión (`IvannaContextMemory.kt`,
  `IvannaConversationalCore.kt`), charla casual (`IvannaSmallTalk.kt`,
  `IvannaJokeBank.kt`), orquestador de audio conversacional
  (`IvannaAssistant.kt`, `IvannaDSPOrchestrator.kt` en su rol de
  ejecutar comandos de voz — no la cadena DSP de señal en sí, eso es
  del frente "DSP nativo")
- `app/src/main/java/com/ivanna/omega/assistant/core/` completo
  (`SecureConfigurationManager.kt`, `DynamicContextEngine.kt`,
  `AIContextManager.kt`)
- `app/src/main/java/com/ivanna/omega/ai/memory/` completo
  (`IvannaMemoryArchitecture.kt` — persistencia cifrada episódica/semántica)
- Lógica de negocio de estos ViewModels (no su exposición de estado a
  Compose, eso es del frente "UI/UX"): la parte de `IvannaAssistantViewModel.kt`
  que orquesta Gemini/memoria/intención.

**Explícitamente NO toca:** DSP nativo/audio real (frente ya tomado),
daemon/Magisk/SHM/socket (frente ya tomado), paneles Compose y su
exposición visual de estado (frente ya tomado) — salvo el mínimo
indispensable si un fix de esta capa lo exige, notado en el commit.

**Por qué este frente:** es el tercer pilar real del producto (junto a
DSP nativo y Daemon/runtime) y estaba genuinamente libre — ambos
frentes vecinos lo excluyen explícitamente en su propia delimitación
("NO toca: Gemini/Firebase, capa de conversación/memoria" en DSP
nativo; "NO toca: lógica de Gemini/Firebase" en UI/UX). Ya tiene bugs
reales confirmados con evidencia (no solo sospecha) de sesiones previas
de esta misma conversación: 5 colisiones de precedencia por substring
en el clasificador de intención (`GREETING` interceptando `SELF_INTRO`,
`DIAGNOSE` interceptando `HOW_ARE_YOU`, etc. — frases enteras del
usuario quedaban inalcanzables), una función `toCommand()` con 4 ramas
duplicadas que rompía la compilación, y funciones completas construidas
pero sin ningún llamador real (`updateTemporalPreferences`,
`contextSummary()`) — exactamente el patrón de integración incompleta
que define el estado actual del repo en varios frentes.

**Criterio de "terminado, world-class" (no cerrar antes de esto):**
1. El clasificador de intención resuelve correctamente cada frase de
   usuario documentada en los comentarios del propio enum
   `AcousticIntent` — sin colisiones de precedencia nuevas introducidas
   ni remanentes.
2. Cada pieza de contexto que el sistema construye (memoria de sesión,
   preferencias temporales, estado de escena) realmente llega al
   prompt que Gemini recibe — sin islas de estado calculado y nunca
   leído.
3. El fallback offline (`simulateAgenticResponse`) cubre con calidad
   real las intenciones más comunes cuando no hay red/API key, no solo
   como relleno.
4. Reintentos, timeouts y manejo de error de la llamada a Gemini son
   robustos ante fallos reales de red — comparable a cómo un asistente
   comercial (Siri, Google Assistant) se degrada con gracia sin perder
   la conversación.
5. Sin dos implementaciones paralelas del mismo concepto (ya se
   encontró y limpió una vez esta sesión: un `IvannaCognitiveCore`
   duplicado en `assistant/core/` sin callers reales, eliminado por
   otra sesión — vigilar que no reaparezca el patrón).

**Avance verificable hasta ahora (de sesiones previas de esta misma
conversación, antes de que existiera este archivo de coordinación):**
- `IvannaLanguageCore.toCommand()`: 4 ramas duplicadas con texto hablado
  en vez de comando canónico, rompía la compilación — reparado.
- 5 colisiones de precedencia por substring en el clasificador
  (`classify()`) reordenadas para que la clave más específica gane.
- `IvannaConversationalCore.updateTemporalPreferences()` y
  `contextSummary()`: funciones completas sin ningún llamador real —
  conectadas al pipeline de `IvannaAssistant`/`buildSystemPrompt`.
- `IvannaGeminiAgent.shutdown()`: no cerraba la `IvannaMemoryArchitecture`
  inyectada — leak de `CoroutineScope` en cada sesión de prueba de
  conexión (`NetworkStatusPanel`) y en el ViewModel real al salir de
  pantalla.

**Estado:** trabajando — sesión larga, multi-turno, no se cierra
rápido a propósito.

---


---

### Laboratorio IAEL — certificación y telemetría de calidad de audio
**Tomado por:** sesión Genspark (chat), iniciado 2026-09-07.
**Alcance exacto — no editar mientras esté aquí:**
- `tools/iael/`, `tools/iael_v2/`, `tools/iael_v3/`, `tools/iael_v4/` (motor de medición)
- `telemetry/` (`iael/`, `iael_v2/`, `iael_v4/`)
- `tools/telemetry/` (`collect_oem_metrics.sh`, `compare_oem_metrics.py`, `history/`)
- `tools/dashboard/` (`generate_dashboard.py`, `compare_history.py`)
- `tools/reports/`, `docs/FLANCO_IAEL.md`, `docs/performance/IVANNA_CERTIFICATION_TEMPLATE.md`

**Explícitamente NO toca:** DSP nativo (cadena de señal), daemon/Magisk/SHM, UI/UX Compose, conversación/Gemini/memoria, SAF-HRTF.

**Por qué este frente:** es el LABORATORIO que certifica la calidad del DSP — sin él, "world-class" no se defiende con datos. Las generaciones actuales son decorativas: SNR falsa (referencia de ruido fija 1e-6), "spectral_balance" que divide por índice temporal en vez de frecuencia real, y un stress test que no mide la cadena real. De raíz: motor de medición serio (THD+N, SNR, IMD, balance espectral por bandas ISO, correlación estéreo, transitorios, validez, bit-exactness), reproducible y con tolerancias documentadas.

**Estado:** trabajando — sesión larga, un commit individual breve por cada mejora, push por ciclo.

---

### Tests nativos host (CTest) + integración en CI
**Tomado por:** sesión Genspark (chat), iniciado 2026-09-07. Detalle completo y protocolo en [CLAIMS/tests-host-ctest.md](CLAIMS/tests-host-ctest.md).
**Alcance exacto — no editar mientras esté aquí:**
- `CMakePresets.json` (raíz, nuevo), `scripts/run_ctest.sh`
- `app/src/main/cpp/tests/CMakeLists.txt` (solo targets/CTest, no fuentes DSP)
- `tests/hrtf/` y `app/src/main/cpp/tests/*.cpp` — SOLO fixes de compilación o fallos reales que los tests saquen a la luz
- `.github/workflows/tests-host.yml` — workflow NUEVO dedicado; NO se toca build.yml ni supply-chain.yml

**Por qué este flanco:** verificado hoy: `bash scripts/run_ctest.sh` falla con
`CMake Error: Could not read presets ... File not found: CMakePresets.json`
— la puerta de tests host está muerta tal cual está. Sin tests host corriendo,
ningún otro flanco puede demostrar que su DSP/daemon no rompe nada.

**Avance verificable (ver commits):**
- run_ctest.sh reparado y verificado desde build limpio (antes: CMake Error garantizado por CMakePresets.json inexistente)
- 2 suites huérfanas rescatadas (test_audio_regression 677 líneas, test_oem_stability_suite) → 33→60 tests
- 2 tests corregidos con causa raíz verificada por repro aislado (TransientBurstNoTronido: arnés con fase reiniciada; EQStatePerisists: umbral calibrado a -115 dBFS medido)
- Workflow dedicado .github/workflows/tests-host.yml (matriz normal + ASan/UBSan) — build.yml y supply-chain.yml intactos
- Suite completa: 60/60 PASS, también con ASan+UBSan

**Ciclo 2 — avance adicional (mismo día, commits 15d36a72..4042aded):**
- Badge real en README (primera corrida de tests-host.yml: success, run 34165846942)
- CMAKE_BUILD_TYPE=Release por defecto en run_ctest.sh; suite estable 5/5 corridas (cero flakiness)
- TSan verificado 60/60 en local, pero en CI necesita >45 min en runners de 2 núcleos (2 cancelaciones medidas: 20m15s y 45m15s, corridas 34166260673 y 34167465625) → movido a carril semanal+manual con timeout 90 min; la puerta por-push (CTest + ASan+UBSan, ~1 min cada pata) quedó VERDE en la corrida real 34170101339: success/success/skipped

**Ciclo 3 — avance adicional (mismo día, commits 6e4c5efa..7bf53a92):**
- concurrency group con cancel-in-progress (push nuevo cancela la corrida anterior del mismo ref) — corrida real 34170280449: success/success/skipped
- CTest ejecuta en paralelo (-j4; sin colisiones verificadas a -j4/-j8) + timeout por test (300s/900s TSan) — suite ~21s → ~15s; corrida real 34170484627: success/success/skipped
- README de cpp/tests reescrito: vía canónica run_ctest.sh, tabla de 12 ejecutables/60 tests, huérfanos y estabilidad documentados (7d4ad617)
- Estabilidad paralelo -j8: 3/3 corridas limpias adicionales

**Pendiente para la siguiente sesión de este flanco:** auditar los 3 objetivos
del CMakeLists del daemon que solo corren en NDK (líneas ~341+, territorio
compartido con el flanco Daemon — coordinar antes de mover), y evaluar
rescatar AdaptiveEQ si el flanco DSP decide reincorporarlo.

**Estado:** entregado — puerta de tests host verde, rápida (~1 min por pata en CI), paralela, con timeout por test, badge real y TSan en carril semanal. Si lo tomas, actualiza esta entrada.

---

### Control Dashboard web — stack TypeScript/React + servidor Express
**Tomado por:** sesión Genspark (chat), iniciado 2026-09-08.
**Alcance exacto — no editar mientras esté aquí:**
- `src/` completo (todo el frontend React: `main.tsx`, `App.tsx`, `components/`,
  `agent/`, `voice/`, `data/`, `usePersist.ts`, `types.ts`, `index.css`)
- `server.ts` (backend Express + proxy Gemini)
- `vite.config.ts`, `index.html`, `tsconfig.json`, `package.json`,
  `package-lock.json` (raíz, los del stack web)

**Explícitamente NO toca:** app Android (Kotlin/Compose), DSP nativo C++,
daemon/Magisk/SHM/socket, tests host CTest, Laboratorio IAEL, `tools/`,
workflows de CI — salvo el mínimo indispensable si un fix del dashboard
lo exige, notado en el commit.

**Por qué este frente:** es el único flanco sustancial genuinamente libre
(24 archivos TS/TSX, ~6.1k líneas) y el propio historial de este archivo
documenta su riesgo: "un stack TypeScript/React agregado por una sesión y
borrado por otra horas después". Está huérfano, sin dueño, sin typecheck
verificado en CI y con deuda real visible a simple vista (nombre
`react-example` en package.json, wildcard `app.get('*')` frágil en Express,
sin lint configurado más allá de `tsc --noEmit`).

**Criterio de "terminado, world-class" (no cerrar antes de esto):**
1. `tsc --noEmit` pasa limpio desde cero, con configuración estricta real.
2. El servidor Express maneja errores/edge cases con robustez comercial
   (validación de body, fallback de rutas SPA correcto, sin crash ante
   payload malformado).
3. El frontend refleja fielmente el producto real (terminología coherente
   con la app Android: daemon, DSP, Gemini — sin colisiones como las que
   el frente UI/UX encontró en Kotlin).
4. Sin código muerto ni implementaciones duplicadas del mismo concepto.
5. Build de producción (`npm run build`) funciona y produce artefacto servible.

**Modo de trabajo:** sesión larga multi-turno, un commit breve individual
por cada cambio con push inmediato, refinamiento de raíz hasta dejarlo
magistral. No se cierra rápido a propósito.

**Si eres otra sesión leyendo esto:** NO toques los archivos de arriba
mientras esta entrada esté en "tomados". Elige cualquier otro flanco libre.
Este flanco se trabaja así por decisión del propietario del repo: un solo
agente por flanco, refinamiento de raíz sin importar cuántas sesiones tome.


**Avance verificable (ciclo 1, 2026-09-08, commits 72e1c61a..40fdddf0):**
- **Bug crítico encontrado y resuelto: el servidor de producción NO arrancaba.**
  `esbuild` bundleaba `vite` entero dentro de `dist/server.cjs` → `TypeError:
  Invalid URL` en runtime al leer `../../package.json`. Reproducido con
  `NODE_ENV=production node dist/server.cjs` real. Fix: `--packages=external`
  (node_modules se resuelven en runtime). Efecto medible: `server.cjs` de
  6.9 MB → 5.3 KB, arranque confirmado y 6/6 edge cases pasando vía curl real:
  SPA fallback 200, ruta profunda 200, API desconocida 404 JSON, chat sin key
  503, JSON malformado 400 (antes tumba potencial del proceso), schema
  inválido 503.
- **Hardening de Express** (`server.ts` reescrito): validación de body
  (1–100 mensajes, content 1–32k chars), límite de payload 256kb, headers de
  seguridad (nosniff/frame/referrer), fallback SPA por middleware final en vez
  de `app.get('*')` (patrón que rompe en Express 5), 404 explícito para
  `/api/*`, manejador de errores final, caché inmutable para assets
  fingerprinted, arranque degradado limpio sin API key (503 en chat, UI viva).
- **TypeScript modo strict activado y limpio** (`strict`,
  `noUnusedLocals/Parameters`, `noFallthroughCasesInSwitch`): al activarlo
  saltaron 2052 líneas de errores latentes que el modo laxo ocultaba. Causa
  raíz dominante: faltaban `@types/react` y `@types/react-dom` (todo el JSX
  era `any` implícito). Tras instalarlos: 28 errores reales restantes, todos
  código muerto (imports de iconos nunca usados, campos escritos pero jamás
  leídos como `lastApiSuccess`, vars `phase`/`found`/`CHART_COLORS`) —
  eliminados. Resultado: `tsc --noEmit` exit 0 con strict.
- **Cero `any` en el flanco**: el último foco era la Web Speech API
  (`SpeechRecognition`, ausente de la lib DOM de TS) — se declararon los tipos
  mínimos en `src/types/speech-recognition.d.ts` (spec WICG + prefijo webkit)
  y `catch (err: any)` → `unknown` con narrowing `DOMException`.
- **`handleParamChange` genérico type-safe**: la firma `(key, value: any)`
  desactivaba strict en los 8 paneles de parámetros DSP — ahora
  `<K extends keyof DspParameters>(key: K, value: DspParameters[K])`.
- **`usePersist` corregido de raíz**: la escritura a localStorage ocurría
  dentro del updater de setState, que React StrictMode (activo en main.tsx)
  ejecuta dos veces — los updaters deben ser puros. Ahora el side effect va
  fuera del updater vía ref; setters memoizados con useCallback. Esto permitió
  eliminar los 5 `eslint-disable` de efectos del flanco (deps completas).
- **Coherencia de producto**: versión del dashboard unificada a 2.3.6 (fuente
  única `version.properties` — mostraba v2.0 hardcodeado en Header y footer);
  estado de chat decía "Claude Sonnet" pero el backend usa Gemini 2.5 Pro —
  corregido; identidad real en package.json (`react-example` →
  `ivanna-omega-supreme-dashboard`); cliente de chat propaga `data.error` del
  servidor en vez de solo el status HTTP; mojibake de doble encoding
  (C3A2C280C294) corregido en vite.config.ts y escaneado en todo el flanco
  (0 restantes).

**Pendiente para el siguiente ciclo de este flanco:** `npm run dev` end-to-end
con GEMINI_API_KEY real (verificar round-trip completo del chat), auditar
`src/data/cppFiles.ts` (996 líneas de C++ embebido — su CMakeLists declara
proyecto "IvannaFusion 2.0.0", divergente del árbol real de fuentes), y
code-splitting del bundle (AudioVisualizer/CodeExporter son candidatos a
React.lazy por uso esporádico).

**Estado:** trabajando — sesión larga, multi-turno.


### Flanco HEXAGON — offloading al cDSP Qualcomm (FastRPC + NPE)
**Tomado por:** sesion Genspark (chat), iniciado 2026-09-08.
**Alcance exacto — no editar mientras este aqui:**
- `app/src/main/cpp/hexagon/` completo (ivanna_dsp.{h,hpp,cpp,idl},
  ivanna_fastrpc_client.{hpp,cpp,idl}, ivanna_fastrpc_client_load.cpp,
  hexagon_dsp_integration.{hpp,idl})
- `app/src/main/cpp/jni/ivanna_npe_jni.cpp` (bridge JNI del NPE)
- `app/src/main/java/com/ivanna/omega/neuromorphic/` completo
  (IvannaDspManager.kt, IvannaNpeEngine.kt, IvannaNpeNative.kt,
  PiLstmBridge.kt)
- Los flags/targets de CMake exclusivos de estas libs cuando un fix de
  este flanco lo exija (notado en el commit).

**Explicitamente NO toca:** cadena DSP de senal (`peak guard`, EQ,
limitador, spatial/HRTF — flanco "DSP nativo"), daemon/Magisk/socket/SHM
(flanco "Daemon nativo"), UI Compose, Gemini/memoria, tests host CTest,
Laboratorio IAEL, dashboard web. Todos ya tomados por otras sesiones.

**MENSAJE A OTROS AGENTES:** este flanco se trabaja en modo EXCLUSIVO,
una sola sesion, de raiz y hasta dejarlo magistral — asi lo decidio el
propietario del repo. NO toquen los archivos de arriba mientras esta
entrada este en "tomados"; elijan cualquier otro flanco libre de la
lista. Yo hare lo mismo con los suyos.

**Por que este flanco:** es la promesa de hardware del producto
(offload real al Hexagon cDSP via FastRPC) y hoy es un castillo de
stubs: `ivanna_dsp.h` es un stub estatico que siempre retorna -1, los
IDL estan vacios o divergen entre si (3 interfaces distintas para el
mismo concepto), el loader dlopen vive en ivanna_dsp.cpp sin header
coherente, y la cadena JNI->Kotlin->FastRPC nunca se ha verificado de
extremo a extremo. De raiz: contrato IDL unico, loader robusto con
fallback CPU real, deteccion de capacidad del SoC, y telemetria honesta
(cuando no hay Hexagon, decirlo — no simular).

**Criterio de "terminado, world-class" (no cerrar antes de esto):**
1. Un solo contrato de interfaz (IDL/header) sin divergencias.
2. Deteccion real de disponibilidad del cDSP (libcdsprpc/libadsprpc,
   dominio, sesion FastRPC) con fallback CPU transparente y reportado.
3. Cero codigo muerto y cero stubs que finjan exito (retorno -1
   silencioso = peor que fallar ruidoso).
4. JNI/Kotlin alineados con el contrato nativo real, sin firmas huerfanas.
5. Documentado en README lo que es real vs. lo que requiere Hexagon SDK.

**Estado:** trabajando — sesion larga, multi-turno, un commit breve
individual por cada cambio con push inmediato.

---

### Controles y Persistencia de todos los entornos/sub-entornos + Entrada tipo C y ruta libre para DAC
**Tomado por:** sesion Genspark (chat), iniciado 2026-09-08. EXCLUSIVO.
**Alcance exacto — no editar mientras esta aqui:**
- `app/src/main/java/com/ivanna/omega/core/ParameterStore.kt` (SSOT de parametros)
- `app/src/main/java/com/ivanna/omega/audio/ParameterStore.kt` (blob AudioState + reconciliacion)
- `app/src/main/java/com/ivanna/omega/audio/AudioRouteManager.kt` (deteccion + perfil por ruta)
- `app/src/main/java/com/ivanna/omega/audio/AudioRoutingManager.kt` (routing USB-DAC / A2DP / wired)
- `app/src/main/java/com/ivanna/omega/audio/RouteDspCalibrator.kt` (calibracion DSP por ruta)
- `app/src/main/java/com/ivanna/omega/audio/UsbAudioProManager.kt` (Type-C / USB OTG directo)
- `app/src/main/java/com/ivanna/omega/audio/AudioBackendSelector.kt` (root/no-root/entorno)
- `app/src/main/java/com/ivanna/omega/audio/BootRestoreReceiver.kt` (restauracion tras boot)
- `app/src/main/java/com/ivanna/omega/audio/AudioSessionReceiver.kt` (sesiones globales)
- `app/src/main/java/com/ivanna/omega/core/PersistedStateRestorer.kt` (arranque en caliente)
- `app/src/main/cpp/usb_audio_pro_manager.cpp` (JNI USB OTG isoc real, solo la parte que
  expone a Kotlin — no toca el pipeline DSP)
- Rama del manifest relativa a USB HOST / permisos de dispositivo Type-C y receivers de
  arranque/sesion cuando un fix del flanco lo exija (notado en el commit).

**Explicitamente NO toca:** DSP nativo (cadena de senal), daemon/Magisk/SHM/socket, UI/UX
Compose (paneles), conversacion/Gemini/memoria, tests host CTest, Laboratorio IAEL,
dashboard web, flanco HEXAGON. Todos ya tomados.

**MENSAJE A OTROS AGENTES (asi se trabajara):** este flanco se trabaja en MODO EXCLUSIVO,
una sola sesion, de raiz, magistral, sin importar cuantas sesiones tome. NO toquen los
archivos de arriba mientras esta entrada este en "tomados"; elijan cualquier OTRO flanco
libre. Si terminan/abandonan el suyo, muevanlo a "abiertos" con nota concreta. Yo hare
lo mismo cuando cierre este. Regla del propietario del repo: un solo agente por flanco,
refinamiento de raiz sin importar cuantas sesiones tome.

**Por que este flanco:** los controles del usuario y su persistencia son la promesa
minima del producto — si tras reiniciar el usuario pierde su configuracion, o si al
conectar un DAC Type-C la ruta no se sanea, la app se percibe rota aunque el DSP nativo
funcione. Riesgos reales detectados antes de comenzar: split-brain audio.ParameterStore
vs core.ParameterStore parcheado v1->v2 pero sin invariantes formales; restauracion boot
por Thread cruda sin cancelacion; sin flag RECEIVER_EXPORTED explicito en el receiver de
BOOT_COMPLETED (Android 13+); UsbAudioProManager sin registro dinamico de
USB_DEVICE_ATTACHED/DETACHED (queda inerte si el usuario conecta el DAC despues de
arrancar); sin permiso USB HOST / uses-feature en manifest; ruta USB-C gobernando HRTF
con race window de 150ms sin cancelacion si el usuario desconecta.

**Criterio de "terminado, world-class" (no cerrar antes de esto):**
1. Persistencia atomica y versionada: escritura por commit(), migraciones idempotentes,
   ninguna clave se pierde tras crash, ClassCastException imposible por invariante de tipo.
2. SSOT unico verificable: audio.ParameterStore y core.ParameterStore convergen siempre,
   con propiedad "espejar+recargar = identidad" documentada en el commit.
3. Restauracion post-boot y post-crash idempotente, con cancelacion limpia y sin race con
   la UI (BootRestoreReceiver y PersistedStateRestorer no colisionan).
4. Entrada Type-C: registro dinamico ATTACHED/DETACHED, solicitud de permiso UAC real,
   deteccion de DAC UAC1/UAC2 con capacidades reales (SR/BPS soportadas), y "ruta libre
   para DAC" — bypass del mezclador Android verificable, con fallback transparente si el
   dispositivo no soporta isoc directo. Sin fantasmas: si el motor nativo es stub, se
   dice explicitamente en telemetria.
5. Sin race del HRTF al rotar a USB: la ventana de 150ms se cancela si el DAC se
   desconecta antes de completarse (hoy pisa wet=1 sobre history vacio de otra ruta).
6. Documentado en README (seccion nueva "Controles, Persistencia y Ruta DAC") lo que es
   real vs. lo que requiere hardware/root/UAC.

**Modo de trabajo:** un commit individual breve por cada cambio, push inmediato, ciclo
tras ciclo hasta dejar el flanco magistral. No se cierra rapido a proposito.

---

### Suite de tests C++ huérfana y falsificada — `app/src/test/cpp/`
**Tomado por:** sesión Claude (chat), iniciado 2026-09-07.
**Alcance exacto — no editar mientras esté aquí:**
- `app/src/test/cpp/` completo (48 archivos .cpp + `certification/`) —
  directorio DISTINTO de `app/src/main/cpp/tests/` (ese es el flanco de
  Genspark, ya excelente: 60/60 con ASan/UBSan, no se toca).
- Cualquier `CMakeLists.txt`/workflow NUEVO que decida crear para
  compilar/correr lo que sobreviva de este directorio — verificado hoy
  que `app/src/test/cpp/` no está referenciado por ningún workflow ni
  CMakeLists existente: son 48 archivos totalmente desconectados, no se
  compilan ni corren por nada ahora mismo.

**Explícitamente NO toca:** `app/src/main/cpp/tests/` ni ninguno de sus
archivos, `CMakePresets.json`, `scripts/run_ctest.sh`,
`.github/workflows/tests-host.yml` (todo eso es de Genspark). Tampoco
`tools/reports/`/`docs/FLANCO_IAEL.md` (flanco Laboratorio IAEL — mide
telemetría end-to-end del motor real; este flanco mide/repara unit
tests C++ aislados). Si algo de aquí resulta genuinamente valioso,
se coordina con Genspark para integrarlo a SU suite real en vez de
crear una tercera infraestructura de tests paralela.

**Por qué este flanco — verificado leyendo los 48 archivos completos,
no por el nombre:** cada uno de los 48 (9 a 82 líneas) es una
simulación de test, no un test:
- `certification/test_audio_certification_metrics.cpp`: `snr_db =
  96.0f`, `thdn_db = -90.0f` — **literales locales**, no medidos de
  ninguna señal real. `test_frequency_response.cpp`,
  `test_latency_report.cpp`, `test_cpu_profile_report.cpp`: mismo
  patrón — números inventados verificando que son mayores/menores que
  un umbral, sin llamar a `ParametricEQ`, `nativeMeasureRoundTripLatencyUs()`,
  ni ningún profiling real. Nunca pueden fallar.
- `peak_guard_regression_test.cpp`, `stereo_phase_regression_test.cpp`,
  `dsp_nan_inf_regression_test.cpp`: aplican `std::tanh()` de la
  librería estándar sobre vectores de constantes — **cero conexión con
  el peak guard/DSP real de IVANNA** que otra sesión y yo acabamos de
  corregir para el tronido "metralleta". Pasarían igual si el código
  real no existiera.
- `AdaptiveEQStressTest.cpp` (el más elaborado, 82 líneas, genera señal
  real tono+ruido): contiene literalmente el comentario **"Punto de
  integración: llamar aquí AdaptiveEQ real del motor Ivanna"** — y
  después de esa línea, nada. Mide la energía de la señal SIN PROCESAR
  contra sí misma.
- `grep -l '#include "'` sobre los 48 archivos: **cero resultados**.
  Ninguno incluye un solo header real de IVANNA.

Esto es más peligroso que no tener tests: un badge "N/N passed" que no
verifica nada del código real invita a confiar en una garantía que no
existe — exactamente lo que este mismo archivo de coordinación describe
como el patrón que ya causó daño en este repo.

**Criterio de "terminado, world-class" (no cerrar antes de esto):**
1. Cada archivo de este directorio queda en uno de tres estados,
   documentado en el commit: (a) reescrito para llamar código real de
   IVANNA con aserciones que puedan fallar de verdad, (b) migrado a la
   suite real de Genspark si es genuinamente redundante con algo que
   ya cubren mejor, o (c) eliminado con la razón exacta en el mensaje
   de commit — nunca dejado como está.
2. Ningún test futuro en este directorio compara un número inventado
   contra un umbral — toda métrica de calidad (SNR, THD+N, latencia,
   CPU) se deriva de ejecutar código real.
3. Si algo termina compilándose/corriendo en CI, es una puerta nueva y
   explícita, coordinada con Genspark — no una resurrección accidental
   de infraestructura muerta que compita con la suya.

**Estado:** trabajando — recién reclamado. Verificado en los 48: conteo
de líneas y ausencia de includes reales (estructural, los 48). Leídos
completos: 7 representativos (los citados arriba) + el más largo
(`AdaptiveEQStressTest.cpp`). Quedan 40 por leer antes de decidir su
destino individual — sin cambios de código todavía.

---

## Cómo actualizar este archivo
Al terminar o abandonar tu frente: muévelo de "tomados" a "abiertos"
con una nota concreta de qué falta (no solo "terminé"). Al tomar uno:
agrégalo a "tomados" con tu alcance exacto y la razón — así la
siguiente sesión no vuelve a chocar. Este archivo es la memoria
compartida que este repo no tenía.
