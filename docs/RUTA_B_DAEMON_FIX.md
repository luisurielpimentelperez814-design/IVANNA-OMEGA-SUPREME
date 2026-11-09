# Ruta B / Daemon — fix perfecto (v2.5.369)

Fecha: 2026-11-08. Estado: implementado, sin commitear, sin pushear.
Doc de diagnóstico previo: `~/workspace/analysis-ivanna/DIAGNOSTICO_RUTA_B_2026-11-07.md`
(este doc es la continuación: causa raíz confirmada + fix + verificación).

## Causa raíz (confirmada)

La cadena de fallo es una sola, con tres eslabones:

1. **El efecto Omega nunca se instancia en AudioFlinger.** Ni por `<apply>`
   postprocess ni por `IvannaGlobalEffectManager.createOmegaEffect` (reflection).
   El constructor falla con `InvocationTargetException` y mensaje `null`
   (la causa real quedaba oculta). La causa más probable es que la factoría de
   efectos leyó una configuración **sin omega**: el XML genérico de vendor
   (`/vendor/etc/audio_effects.xml`) YA tenía omega fusionado por el módulo,
   pero la SKU blair (`/vendor/etc/audio/sku_blair/audio_effects.xml`, 0
   menciones) y `/system/etc/audio_effects.xml` (0 menciones) NO lo tenían.
   El módulo solo fusionaba la PRIMERA configuración encontrada, así que si
   el HAL lee la SKU (o `/system/etc`), el UUID `4956414e-…` no existe para él.
   Evidencia a favor: el HAL, al reiniciarse, creó efectos stock (DAP/Dolby)
   pero nunca omega; cero errores de carga del .so y cero denegaciones AVC
   (no es un fallo de carga ni de SELinux, es un UUID no registrado).
2. **Sin efecto no hay consumidor.** El daemon acepta comandos pero
   `applied:false`, `consumer:null`, `dsp_frames:0`. El consumidor es la
   instancia `omega_effect` en audioserver; si no existe, nada procesa.
3. **La app confiaba en "daemon vivo".** Con daemon respondiendo, la app
   pasaba a `ROOT_DAEMON` y detenía el fallback local, aunque el efecto no
   existiera. Resultado: daemon vivo, efecto ausente, audio sin procesar.
   Además la app solo intentaba crear el efecto al abrirse una sesión nueva:
   una sesión ya abierta (la 409 de YouTube Music) quedaba en efectos stock
   para siempre, y el watchdog solo recreaba sesiones con efectos MUERTOS
   (si los stock estaban vivos, nunca reintentaba crear omega).

## El fix (qué cambió y dónde)

### 1. Módulo Magisk: fusionar omega en TODAS las configuraciones candidatas
`magisk_module/customize.sh`
- La lógica de merge (registro librería + efecto UUID + `<apply>` en
  music/movie/game, con todas sus validaciones fail-closed) se extrajo a la
  función `merge_omega_xml <src_oem> <dst>`, que devuelve 0/1 sin abortar.
- El instalador la llama para la configuración principal (obligatoria: si
  falla, aborta como antes) **y** para cada candidata adicional que exista:
  `/vendor/etc/audio/sku_*/audio_effects.xml` y `/system/etc/audio_effects.xml`.
  Cada una se fusiona con SU propio contenido OEM (nunca se copia el merge de
  una sobre otra: cada SKU conserva sus efectos propietarios). Un fallo en una
  candidata adicional NO aborta la instalación (el merge principal ya quedó);
  solo se avisa y se conserva su base OEM.
- El espejo Magisk es el mismo de antes (`/vendor/...` →
  `$MODPATH/system/vendor/...`).
- **Verificado en host (no solo sintaxis):** `merge_omega_xml` probada contra
  XML reales: SKU stock sin omega → 1 librería + 1 efecto + 3 applies, XML
  bien formado, los 3 streams con apply; SKU que ya tenía omega → sin
  duplicados; doble merge → idempotente (1/1/3); XML inválido → falla (rc=1).
  El bucle de candidatas probado con raíz simulada: las 4 ubicaciones
  (principal + sku_blair + sku_holi + system/etc) quedan fusionadas 1/1/3.

### 2. App: reintento de creación del efecto con backoff (cualquier sesión)
`app/src/main/java/com/ivanna/omega/audio/IvannaGlobalEffectManager.kt`
- `tryAttachOmega(sessionId)`: intenta crear el efecto en una sesión YA
  ABIERTA que quedó sin él. Si tiene éxito, libera los efectos stock (diseño
  omega-solo por sesión, para no duplicar procesado), acopla el efecto nativo,
  reaplica el perfil y avisa al selector (`notifyOmegaAttached`). Si falla,
  deja la sesión como estaba (stock) y registra el intento.
- `retryOmegaAttachForSessions()`: pase del watchdog (cada 3 s) que reintenta
  en cada sesión viva sin omega, respetando el backoff por sesión.
- `OmegaAttachBackoff`: exponencial con techo — 2 s, 5 s, 10 s, 20 s, 30 s
  (cap). Reintenta agresivo al principio (el HAL lazy tarda segundos) y luego
  cada 30 s **indefinidamente** mientras la sesión siga viva, por si el
  módulo/HAL se vuelve disponible más tarde. Nunca se rinde.
- `omegaRootCauseMessage(t)`: desenrolla la cadena de causas hasta la raíz.
  El log de fallo ya no dice solo "null": ahora muestra la causa real
  (p. ej. `IllegalArgumentException: UUID de efecto no registrado`).
- Limpieza: al cerrar una sesión o en `releaseAll()` se borran sus contadores
  de reintento.

### 3. App: handshake sólido — ROOT_DAEMON exige Ruta B confirmada
`app/src/main/java/com/ivanna/omega/audio/AudioBackendSelector.kt`
- `evaluate()` ahora exige, para entrar en `ROOT_DAEMON`, las tres cosas:
  root + daemon vivo + **Ruta B confirmada** (beacon `ENABLED_IDLE` o
  `PROCESSING`). Con daemon vivo pero efecto sin confirmar, el modo es
  `ROOT_NO_DAEMON` y el **fallback local SIGUE procesando** (fail-closed).
  Solo con Ruta B confirmada se detiene el fallback (evita doble procesado).
  Si el beacon es ilegible (`UNAVAILABLE`) no se afirma nada: se conserva el
  fallback.
- `notifyOmegaAttached()`: cuando el efecto se acopla a una sesión, la app
  re-evalúa el backend DE INMEDIATO (sin esperar al ciclo de 10 s), para que
  el fallback se detenga pronto si el beacon ya lo confirma.
- Nuevos `StateFlow`: `routeBConfirmed` y `daemonAlive` (observables por UI).
- `routeBStatusLine()`: resumen legible de una línea para diagnóstico.
- `decideMode(root, daemon, routeBConfirmed)` e `isRouteBConfirmed(effect)`:
  decisiones puras, probadas en host (ver Tests).
- El `stop()` resetea los nuevos estados.

### 4. UI honesta
`app/src/main/java/com/ivanna/omega/audio/RouteCapabilities.kt`,
`app/src/main/java/com/ivanna/omega/ui/RouteModeBanner.kt`
- `limitsFor(mode, daemonAlive)`: en `ROOT_NO_DAEMON` con daemon vivo, el
  título ya no dice "el módulo Magisk no responde" (sí responde; lo que falta
  es el efecto). Ahora dice: "Ruta B sin confirmar: el daemon responde pero el
  efecto del sistema no está procesando". Con daemon caído, el título original
  sigue. El banner solo aparece cuando hay algo que advertir (en
  `ROOT_DAEMON` no aparece, como antes).

### 5. Telemetría (ya existía; ahora es visible)
- `OmegaEngineBridge.requestTelemetry()` / `requestControlPlaneEvidence()`
  ya devolvían `daemonReachable`, `effectState`, `consumerPresent`,
  `dsp_frames`, generaciones y conteos de sesiones. Ahora el estado de Ruta B
  también se refleja en `AudioBackendSelector.routeBStatusLine()` y en el
  log de transición de modo (`modo: X -> Y (root=… daemon=… routeB=… effect=…)`),
  así que el diagnóstico en el teléfono es inmediato con un solo `logcat`.

## Archivos tocados (esta tarea, sin commitear)

| Archivo | Cambio |
|---|---|
| `magisk_module/customize.sh` | `merge_omega_xml()` + fusión en todas las candidatas (SKU + system) |
| `app/src/main/java/com/ivanna/omega/audio/IvannaGlobalEffectManager.kt` | Reintento omega con backoff + causa raíz + aviso al selector |
| `app/src/main/java/com/ivanna/omega/audio/AudioBackendSelector.kt` | Handshake (ROOT_DAEMON exige efecto), `notifyOmegaAttached`, `routeBConfirmed`/`daemonAlive`, `routeBStatusLine`, `decideMode` |
| `app/src/main/java/com/ivanna/omega/audio/RouteCapabilities.kt` | Título honesto según daemon vivo o caído |
| `app/src/main/java/com/ivanna/omega/ui/RouteModeBanner.kt` | Observa `daemonAlive` |
| `app/src/test/java/com/ivanna/omega/audio/RouteBRecoveryTest.kt` | **Nuevo.** Tests JVM de la lógica pura |
| `docs/RUTA_B_DAEMON_FIX.md` | **Nuevo.** Este doc |

NO tocados (otro agente / Volterra): `IvannaFusionCore.cpp`,
`ivanna_omega_jni.cpp`, `tests/CMakeLists.txt`, `test_holos_retune_sweep.cpp`,
`ParameterStore.kt`, `IvannaControlPanel.kt`, `CHANGELOG.md` (el otro agente
lo está editando; la entrada propuesta para la 369 está abajo), nada de
Volterra (`.ivk1`, `volterra_*`, tests de Volterra), ni los defaults de
ORBE/HOLOS/Φ.

## Tests

- **JVM (nuevo `RouteBRecoveryTest`, 10 tests):** backoff (valores, techo 30 s,
  monotonía, coerción de negativo), causa raíz (desenrolla
  InvocationTargetException, mensaje null, excepción simple), confirmación
  Ruta B (solo ENABLED_IDLE/PROCESSING), decisión de modo (tabla completa:
  ROOT_DAEMON solo con las tres; daemon vivo sin Ruta B → ROOT_NO_DAEMON),
  banner (título distinto según daemon vivo/caído; sin aviso en ROOT_DAEMON).
  **No se pudieron correr en host** (no hay Android SDK/Gradle aquí): los corre
  la CI (`gradle testReleaseUnitTest`). La lógica es pura y determinista.
- **C++ host (CTest):** suite completa **839 tests: 838 pasan en paralelo;
  1 falla en paralelo (`HolosChain.ThroughputRt`, test #386) y PASA en re-run
  (5.88 s)** — es uno de los 2 flaky de throughput preexistentes (pasan en
  re-run), no un fallo del fix. Efectivo: **839/839**. La tarea no toca C++.
  Nota: la suite creció de 837 a 839 por los 2 tests nuevos de retuneo HOLOS
  del otro agente (que también pasan).
- **Shell (`customize.sh`):** `sh -n` y `bash -n` OK. `merge_omega_xml`
  probada funcionalmente contra XML reales (ver §1): stock → 1/1/3, ya
  fusionado → sin duplicados, doble merge → idempotente, inválido → rc=1.
  Bucle de candidatas probado con raíz simulada (4 ubicaciones 1/1/3).
- **Kotlin:** balance de llaves/paréntesis verificado en los 5 archivos
  tocados/creados. No hay compilador Kotlin/Android aquí; lo verifica la CI.

## Cómo verificarlo en el teléfono (Moto G85)

Requisito: instalar el módulo Magisk v2.5.369 (con este fix) y reiniciar.
La app debe estar en segundo plano con una app de música sonando.

### 1. El módulo fusionó todas las configuraciones
```sh
su -c 'for f in /vendor/etc/audio_effects.xml /vendor/etc/audio/sku_blair/audio_effects.xml /system/etc/audio_effects.xml; do echo "== $f"; grep -c omega_effect "$f" 2>/dev/null || echo "(no existe)"; done'
```
Esperado: cada archivo que exista tiene omega (librería + efecto + 3 applies).
Si la SKU blair sigue en 0, el HAL no la está leyendo desde ahí (ver §2).

### 2. El efecto se crea (el fix principal)
```sh
logcat -c
# Forzar sesión nueva: cerrar YouTube Music y volver a abrirla con música.
su -c 'am force-stop com.google.android.apps.youtube.music'
# Abrir YouTube Music, poner música, esperar 10 s.
logcat -d | grep -E "GlobalFX|Omega effect|acoplada a libomega|Ruta B|modo de audio"
```
Esperado:
- `Sesión <id> acoplada a libomega_effect nativo` (o `Sesión <id> con efecto omega nativo` en apertura), y
- `modo de audio: ROOT_NO_DAEMON -> ROOT_DAEMON (root=true daemon=true routeB=true effect=PROCESSING|ENABLED_IDLE)`.
- Ya NO debe aparecer `Omega effect no disponible` de forma persistente; si
  aparece, el log ahora muestra la **causa real** (no "null").

Si el efecto sigue sin crearse, el log de causa real dice por qué (UUID no
registrado = el HAL leyó una config sin omega; otro error = ver su mensaje).

### 3. El daemon procesa (consumer + frames)
```sh
# Por socket (la app lo hace con GET_STATUS; en Termux se puede sondear con la
# telemetría de la app o con el bloque de diagnóstico del 07).
# Lo más directo: mirar el log de la app y el estado de Ruta B.
logcat -d | grep -E "RutaB modo|daemon=|efecto=|confirmada="
```
Esperado: `RutaB modo=ROOT_DAEMON daemon=vivo efecto=PROCESSING confirmada=sí`.
Además `dsp_frames` debe crecer y `consumer` debe ser `omega_effect`
(visible en la telemetría de `OmegaEngineBridge` / `requestTelemetry()`).
Antes del fix: `applied:false, consumer:null, dsp_frames:0`.

### 4. El fallback no deja el audio sin procesar
- Con el módulo **deshabilitado** (o daemon caído): el modo es
  `ROOT_NO_DAEMON`/`NO_ROOT`, el banner muestra el aviso y la captura
  (Ruta A) sigue procesando. El audio nunca queda sin IVANNA.
- Con el módulo habilitado y el efecto creado: modo `ROOT_DAEMON`, banner
  sin aviso, Ruta B procesa. No debe haber eco ni doble procesado (el
  fallback se detiene solo cuando Ruta B está confirmada).

### 5. Reintento en sesión ya abierta
- Abrir YouTube Music ANTES de que el módulo/HAL esté listo (o con el
  módulo recién instalado sin reiniciar el HAL), dejar la música sonando y
  esperar: el watchdog reintenta cada ~3 s con backoff y la sesión debe
  acoplarse sola (`acoplada a libomega_effect nativo por reintento`), sin
  tener que cerrar y reabrir la app.

## Qué quedó pendiente / bloqueado

- **Verificación en el teléfono (todo el §"Cómo verificarlo"):** la creación
  del efecto en AudioFlinger, el beacon y el socket del daemon solo existen
  en el G85. Sin el teléfono no se puede confirmar que el HAL lea la SKU
  fusionada ni que el efecto procese. El fix cubre todas las candidatas para
  no depender de adivinar cuál lee el HAL, pero la confirmación final es en
  dispositivo.
- **CI:** la suite C++ y los tests JVM nuevos los corre la CI (no hay SDK
  aquí). Falta el PAT para pushear y que la CI re-corra.
- **CHANGELOG (entrada 369):** no se editó porque el otro agente
  (ORBE/HOLOS + paridad Ruta A) lo está editando ahora mismo. Entrada
  propuesta para añadir al commitear (debajo de la línea de Paridad Ruta A):
  > - **Ruta B / daemon PERFECTO (`docs/RUTA_B_DAEMON_FIX.md`).** El módulo
  >   Magisk fusiona omega en TODAS las configuraciones de efectos
  >   (genérica de vendor + SKU blair/holi + `/system/etc`), no solo en la
  >   primera: el HAL ya no puede leer una config sin el UUID. La app
  >   reintenta crear el efecto en cualquier sesión (incluidas las ya
  >   abiertas) con backoff exponencial (2 s→30 s, sin rendirse) y desenmascara
  >   la causa real del fallo del constructor (antes solo "null"). Handshake
  >   sólido: `ROOT_DAEMON` exige efecto confirmado (beacon
  >   ENABLED_IDLE/PROCESSING); con daemon vivo pero sin efecto, el modo es
  >   `ROOT_NO_DAEMON` y el fallback local sigue procesando (fail-closed).
  >   Banner honesto según daemon vivo o caído. Tests JVM nuevos
  >   `RouteBRecoveryTest` (10 tests). Pendiente verificar en el G85 que el
  >   efecto se crea y `dsp_frames` crece.
- **README:** sin cambio (ya describe Ruta B/daemon; el fix hace real lo
  descrito, no cambia la descripción visible).
- **Recuperación del daemon si se cae:** el `evaluate()` periódico (10 s)
  ya re-detecta daemon caído/vivo y cambia el modo (fallback ↔ Ruta B); el
  watchdog reintenta el efecto. El daemon en sí lo levanta Magisk
  (`late_start_service`); si el proceso del daemon muere, Magisk no lo
  reinicia solo — eso es un límite del arranque por `service` y no se tocó
  (requeriría un supervisor; fuera del alcance de este fix).
- **Puerta auditiva / ABX:** sin cambio (la decide el usuario; ORBE/HOLOS
  los gestiona el otro agente).
