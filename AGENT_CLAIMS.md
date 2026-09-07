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

## 🟢 Frentes abiertos, sin dueño (elige uno y anótalo arriba)

### Binaural real (HRTFConvolver → pipeline audible)
Investigado a fondo (ver commits de "FASE 8" en el historial). El
crossfade de azimuth ya se corrigió a ley de potencia constante. Lo
que falta: `PdEngine::process_block()` procesa muestra-a-muestra;
`HRTFConvolver` procesa por bloque/FFT — sí se puede llamar
`hrtf.process()` una vez por bloque sin acumulador externo (ya
verificado leyendo el código completo), pero falta el cableado real
con su propio flag de activación, coexistiendo con `CueBasedSpatial`
sin reemplazarlo por defecto, con rampa anti-click al alternar entre
ambos. Trabajo grande, dividir en commits por sub-paso.

### Capa conversacional / Gemini / Firebase AI Logic
Migración base ya hecha: Firebase AI Logic cableado (evita el
problema de keys `AQ.`/`AIza` retiradas por Google), App Check
instalado, 3 fugas de recursos cerradas, errores de mic 11/12
corregidos, modo manos-libres funcional, estado PROCESSING formal.
Pendiente real: el límite de tokens de salida está declarado en el
registry de modelos (`maxOutputTokens`) pero **nunca se aplica** —
no hay `generationConfig` real en la llamada. Falta verificar
end-to-end si "auto-repararse" (`IvannaSelfHealingEngine`) y el
agente conversacional están realmente conectados o es otro caso de
pieza-bien-construida-pero-huérfana (patrón que se repitió varias
veces en este repo — verificar con grep de llamadores reales, no
asumir por el nombre del archivo).

### DSP adaptativo / algoritmos de audio
`ThermalGovernor` vs. decisión de IA ya no colisiona sobre compresor/
spatial_width (FASE 7, patrón de base+escala igual que el EQ). Vale
la pena una auditoría de precisión numérica y casos límite en el
resto de la cadena (exciter, EQ perceptual, peak guard — este último
recién tocado por otra sesión, revisar si sigue coherente).

### UI Compose del panel de asistente
Varios bugs puntuales ya arreglados (estado PROCESSING, manos-libres,
imports duplicados). No ha habido una pasada de diseño/UX real, solo
correcciones — el panel de red (`NetworkStatusPanel`) en particular
mezcla diagnóstico técnico con estado del agente de forma un poco
confusa para un usuario final.

---

## Cómo actualizar este archivo
Al terminar o abandonar tu frente: muévelo de "tomados" a "abiertos"
con una nota concreta de qué falta (no solo "terminé"). Al tomar uno:
agrégalo a "tomados" con tu alcance exacto y la razón — así la
siguiente sesión no vuelve a chocar. Este archivo es la memoria
compartida que este repo no tenía.
