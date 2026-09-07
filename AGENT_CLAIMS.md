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

**Estado:** trabajando — primer fix real ya en main (`c2826986`): la
palabra "IVANNA"/"AGENTE" se usaba para 3 conceptos distintos sin
diferenciar (alcanzabilidad del servidor Gemini, si hay API key
configurada, y el SOCKET del daemon en MagiskStatusPanel) — renombrado
a terminología específica ("GEMINI"/"Servidor Gemini") en las 3
apariciones dentro de mi alcance (`NetworkStatusPanel.kt`). No tocado
`MagiskStatusPanel.kt` (fuera de mi alcance).
Pendiente: revisar si `IvannaAssistantScreen.kt` tiene el mismo tipo de
colisión terminológica; evaluar si los paneles deberían reorganizarse
en categorías claras (Red / IA / Sistema) en vez de solo renombrar.

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

**Estado:** trabajando — commits individuales breves, push por ciclo.

## Cómo actualizar este archivo
Al terminar o abandonar tu frente: muévelo de "tomados" a "abiertos"
con una nota concreta de qué falta (no solo "terminé"). Al tomar uno:
agrégalo a "tomados" con tu alcance exacto y la razón — así la
siguiente sesión no vuelve a chocar. Este archivo es la memoria
compartida que este repo no tenía.
