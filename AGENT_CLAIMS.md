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

**Estado:** iniciando — primer commit en curso.

---

## Cómo actualizar este archivo
Al terminar o abandonar tu frente: muévelo de "tomados" a "abiertos"
con una nota concreta de qué falta (no solo "terminé"). Al tomar uno:
agrégalo a "tomados" con tu alcance exacto y la razón — así la
siguiente sesión no vuelve a chocar. Este archivo es la memoria
compartida que este repo no tenía.
