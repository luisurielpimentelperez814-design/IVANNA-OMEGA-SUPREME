# IVANNA OMEGA SUPREME — Seguridad anti-piratería (interno)

> **CONFIDENCIAL — NO PUBLICAR.** Este documento es interno: solo el usuario
> (Luis Uriel Pimentel Pérez) y su asistente conocen estos detalles. NO se
> referencia en el README público, ni en materiales promocionales, ni en
> publicaciones de redes. NO se commitea información sensible aquí
> (contraseñas, valores de pins, keystores).

## Alcance del producto

IVANNA es para **cualquier Android 13 en adelante**. El Moto G85 es el
dispositivo de desarrollo/prueba, pero el producto apunta a cualquier Android
13+. **Ninguna medida de seguridad asume un dispositivo concreto**: no hay
nada hardcodeado del Moto G85 (ni modelo, ni fingerprint, ni rutas de vendor
propias de Motorola) en el módulo de seguridad ni en su documentación.

## Objetivo

Hacer más difícil piratear el producto que crear uno nuevo. Defensa en
profundidad, todo on-device, sin servidor externo. **No es una garantía
absoluta**: contra un atacante con root y tiempo ilimitado, ninguna
comprobación on-device es inviolable. Lo que se eleva es el coste de:
re-firmar el APK, reutilizar las `.so` en otro paquete, y distribuir copias
modificadas.

## Amenazas cubiertas (y cómo)

| Vía de piratería | Medida | Dónde |
|---|---|---|
| Extracción del APK + re-firma y redistribución | Pin de certificado de firma generado en build-time desde el keystore que firma ESE build; verificación en `onCreate` (fail-closed) | `tools/security/generate_build_pins.py`, `app/build.gradle.kts` (`generateSecurityPins`), `security/IntegrityGate.kt`, `security/ivanna_integrity.{h,cpp}` |
| Decompilación del dex (código Kotlin legible) | R8/ProGuard: se eliminó el keep global `-keep class com.ivanna.omega.**`; solo entry points del framework/reflexión se conservan por nombre | `app/proguard-rules.pro` |
| Ingeniería inversa de las `.so` (símbolos, strings) | `-fvisibility=hidden` (solo JNIEXPORT + AELI en `.dynsym`), `-Wl,-s` (strip de `.symtab`), `-fstack-protector-strong`, RELRO/BIND_NOW/noexecstack en `libivanna_omega.so`, `libomega_effect.so` e `ivanna_daemon` | `app/src/main/cpp/CMakeLists.txt`, `app/src/main/cpp/daemon/CMakeLists.txt` |
| Reutilización de `libomega_effect.so` en otro paquete / sideload de una `.so` copiada | Self-check de ruta de carga (`dladdr`): la `.so` debe venir de `/data/app`, `/vendor`, `/system`, `/odm` o `/product`. En Ruta B, `omega_process` hace passthrough si falla | `security/ivanna_integrity.cpp`, `app/src/main/cpp/omega_effect.cpp` |
| Clonación de la instalación en otro dispositivo (mismo APK firmado) | Vinculación ANDROID_ID (en Android 8+ depende del dispositivo Y de la clave de firma) registrada en el primer arranque, cifrada con EncryptedSharedPreferences | `security/IntegrityGate.kt` |
| Copia del módulo Magisk (zip público) | El módulo ya existía con binding por UID del daemon (`peer_auth.hpp`); el endurecimiento de build (strip, `-fvisibility=hidden`) dificulta extraer y reutilizar sus binarios. El zip del módulo sigue siendo público por diseño (es el canal de distribución de Ruta B) | `app/src/main/cpp/daemon/` |
| Sustitución de `libivanna_omega.so` por una copia modificada (misma firma aparente) | Hash SHA-256 de la `.so` instalada en runtime vs hash generado en build-time (asset `ivanna_so_hashes.txt`); paso 4 de `IntegrityGate.verify()` (fail-closed en release) | `tools/security/generate_so_hashes.py`, `app/build.gradle.kts` (`generateSoHashes`), `security/ivanna_integrity.{h,cpp}` (`integrity_sha256_file`), `security/IntegrityGate.kt` |
| Sustitución de `libomega_effect.so` stageada en el módulo Magisk | El daemon verifica en arranque el hash de la `.so` en `/vendor` contra `ivanna_effect_hash.txt` (generado por CI tras el staging); si no coincide, rechaza todos los `SET_*` (el efecto queda en sus valores por defecto) | `daemon/core/effect_integrity.{h,cpp}`, `daemon/ivanna_daemon.cpp`, `daemon/control/command_server.cpp`, `.github/workflows/build.yml` |
| Análisis estático de las `.so` (`strings`/grep revela qué se comprueba) | Cifrado XOR de strings en tiempo de compilación (`XorString`, `static constexpr` — el literal en claro NO se emite en `.rodata`, verificado con `strings`); predicados opacos + control-flow flattening en el evaluador anti-tamper | `security/ivanna_obfuscate.h`, `security/ivanna_antitamper.{h,cpp}`, `security/ivanna_integrity.cpp` |
| Depurador / Frida / framework de hooks / emulador en el proceso | Anti-tamper (TracerPid, artefactos en `/proc/self/maps`, archivos Frida, propiedades de emulador); en Ruta A bloquea el DSP (paso 5 de `IntegrityGate`); en Ruta B `omega_process` hace passthrough. Root es SOLO señal informativa (el producto está diseñado para dispositivos rooteados) | `security/ivanna_antitamper.{h,cpp}`, `security/IntegrityGate.kt`, `app/src/main/cpp/omega_effect.cpp` |
| Lectura de literales de seguridad en el dex (nombres de prefs, claves, asset) | `SecStrings`: literales de `IntegrityGate.kt` cifrados con XOR de un byte (no aparecen en claro en la tabla de strings del dex); regenerables con `obfuscate_strings.py --check` | `app/src/main/java/com/ivanna/omega/security/IntegrityGate.kt`, `tools/security/obfuscate_strings.py` |

## Módulo de seguridad (archivos)

```
app/src/main/cpp/security/
    ivanna_integrity.h          API nativa (pin, self-check, gate, SHA-256)
    ivanna_integrity.cpp        SHA-256 autocontenido, comparación en tiempo
                                constante, self-check dladdr, gate atómico,
                                SHA-256 por streaming + hash de archivo
                                (fase 2), literales cifrados con XorString
    ivanna_integrity_jni.cpp    Puente JNI para IntegrityGate.kt (7 funciones:
                                5 de fase 1 + nativeSha256File +
                                nativeAntiTamperSignals de fase 2)
    ivanna_obfuscate.h          (fase 2) XorString (XOR compile-time,
                                `static constexpr`), predicados opacos,
                                wipe de buffers
    ivanna_antitamper.h/.cpp    (fase 2) Anti-debug (TracerPid), anti-hook
                                (Frida/Xposed/Substrate/Cydia), root y
                                emulador como señales; evaluador con
                                control-flow flattening
app/src/main/cpp/daemon/core/
    effect_integrity.h/.cpp     (fase 2) Verificación de la .so del efecto
                                stageada contra el hash del módulo
app/src/main/java/com/ivanna/omega/security/
    IntegrityGate.kt            verify() en onCreate: cert + vinculación +
                                self-check + hash .so (paso 4) + anti-tamper
                                (paso 5); isLocked fail-closed; SecStrings
                                (literales cifrados, fase 2)
tools/security/
    generate_build_pins.py      Genera ivanna_build_pins.h (gitignored) desde
                                el keystore de firma; falla si no puede
    generate_so_hashes.py       (fase 2) Genera ivanna_so_hashes.txt
                                (gitignored) con el SHA-256 de
                                libivanna_omega.so; falla si falta la .so
    obfuscate_strings.py        (fase 2) Genera/verifica (--check) el objeto
                                SecStrings de IntegrityGate.kt
docs/SECURITY_INTERNAL.md       Este documento
```

## Flujo de verificación (runtime)

1. `IVANNAApplication.onCreate()` → `IntegrityGate.verify(this)` (primero).
2. Certificado: `PackageManager.GET_SIGNING_CERTIFICATES` → SHA-256 del
   primer firmante → `nativeVerifyCert()` compara (tiempo constante) con el
   pin de build. `NO_PIN` en release → bloqueo; en debug → aviso.
3. Vinculación: ANDROID_ID registrado (cifrado) en el primer arranque. Si
   cambia y el `versionCode` NO subió → bloqueo (re-firma/clonación). Si el
   `versionCode` subió → re-vinculación (actualización legítima; con
   keystore efímero la clave puede cambiar entre builds).
4. Self-check nativo: `dladdr` de la `.so` cargada. Ruta no legítima →
   bloqueo.
5. (Fase 2) Hash de la `.so` en runtime: `nativeSha256File()` hashea la
   `.so` instalada (`nativeLibraryDir/libivanna_omega.so`) y se compara
   (tiempo constante) con el hash del asset `ivanna_so_hashes.txt`
   (generado en build-time por `generateSoHashes`). Si no coincide →
   bloqueo. Si el asset falta: en debug se omite (aviso), en release →
   bloqueo (el build de release debe generar el asset; la CI lo verifica).
6. (Fase 2) Anti-tamper: `nativeAntiTamperSignals()` devuelve el bitmask
   de señales (depurador, Frida, framework de hooks, emulador). Si hay
   alguna señal de lockdown → bloqueo. Root por sí solo NO bloquea (solo
   se registra; el producto está diseñado para dispositivos rooteados).
7. Si todo pasa: `nativeSetGate(true)` → DSP habilitado. Si algo falla:
   `isLocked = true` → los 3 inits DSP no corren (`DSPBridge.init`,
   `MainActivity` bootstrap, `AudioEngine.initialize`) → **audio en
   passthrough, teléfono intacto**. Recuperación legítima: borrar los datos
   de la app vuelve a vincular (primer arranque).

Ruta B (audioserver, sin JNI): `omega_process` llama a
`integrity_self_check()` (cacheado, un atomic load por bloque) al inicio de
cada bloque; si falla → passthrough limpio. (Fase 2) También llama a
`antitamper_should_lockdown()` (cacheado, un atomic load por bloque); si
hay depurador, Frida, framework de hooks o emulador en audioserver →
passthrough limpio. La verificación completa (pin + vinculación + hash .so)
solo ocurre en el proceso app; el efecto no puede verificar el certificado
del APK desde audioserver.

Daemon (root, vía Magisk): en el arranque, `effect_integrity_verify()`
hashea `libomega_effect.so` en `/vendor` y la compara con
`ivanna_effect_hash.txt` (generado por CI tras stagear la `.so` en el
módulo). El resultado se cachea y se escribe en
`/data/adb/ivanna_omega/effect_integrity.status`. Si no coincide
(`mismatch`), el daemon NO aplica parámetros DSP: `CommandServer` rechaza
todas las acciones `SET_*` con error `"effect_integrity_failed"` (las
consultas y la telemetría siguen). Si el módulo no trae el archivo de hash
(módulo anterior a esta función) → aviso y se continúa (compatibilidad
hacia atrás). Si la `.so` no está o no se puede leer → tampoco se pilotan
parámetros (el efecto no está activo de todos modos).

## Build-time

- `generateSecurityPins` corre en `preBuild` (antes de compilar el nativo).
  Usa la misma lógica de keystore que `signingConfig`: `KEYSTORE_FILE` o
  `~/.android/debug.keystore`, `KEYSTORE_PASSWORD`/`KEY_ALIAS` o
  `android`/`androiddebugkey` por defecto. Como el pin se genera del
  keystore que firma ESE build, funciona tanto con keystore estable (secreto
  CI) como con llave efímera por build.
- Si el debug keystore no existe en una máquina limpia (AGP lo genera en el
  paso de firma, DESPUÉS de `preBuild`), el script lo crea con los mismos
  parámetros de AGP (RSA 2048, 10000 días, `CN=Android Debug`) y AGP lo usa
  tal cual — el pin sigue coincidiendo. Un keystore personalizado ausente
  es error de configuración y la tarea falla (fail-closed).
- `ivanna_build_pins.h` es generado y **gitignored** (no se commitea).
- Sin pin generado: `IVANNA_PIN_CERT_SHA256` es cadena vacía → `kNoPin` →
  bloqueo en release, aviso en debug.
- La CI debe verificar el build del APK (Gradle/R8) — no hay SDK/NDK en la
  VM de desarrollo, así que el build completo no se puede correr aquí.

### Fase 2 (build-time)

- `generateSoHashes<Variant>` corre tras `externalNativeBuild<Variant>` y
  antes de `merge<Variant>Assets` (cableado por variante con
  `androidComponents.onVariants`). Hashea `libivanna_omega.so` (una por ABI;
  solo `arm64-v8a` hoy) y escribe `app/src/main/assets/ivanna_so_hashes.txt`
  (gitignored). La tarea FALLA si la `.so` no existe (el script sale con
  error): un APK release sin asset quedaría bloqueado fail-closed por
  `IntegrityGate`, así que es mejor fallar el build. La CI verifica que el
  APK contenga `assets/ivanna_so_hashes.txt` (paso en `build.yml`).
- Hash del efecto: tras stagear `libomega_effect.so` en el módulo, `build.yml`
  escribe `ivanna_effect_hash.txt` (SHA-256 hex, una línea) junto a la `.so`
  en `magisk_module/system/vendor/lib64/soundfx/`. El daemon la lee en
  arranque desde `/vendor/lib64/soundfx/ivanna_effect_hash.txt` (overlay de
  Magisk). El hash también queda registrado en el manifiesto `BUILD_INFO`
  (`effect_sha256`).
- LTO opt-in: `-DIVANNA_SECURITY_LTO=ON` añade `-flto=thin` a la compilación
  y el enlace de `ivanna_omega` y `omega_effect`. Default OFF (el build NDK
  no se puede verificar en la VM; la CI debe verificarlo al activarlo).
- `obfuscate_strings.py --check` verifica que el objeto `SecStrings` de
  `IntegrityGate.kt` coincide con los literales (regenerar si cambian).

## Qué NO cubre (limitaciones honestas)

- **Atacante con root + tiempo**: puede parchear los checks (son
  on-device). El objetivo es elevar el coste, no la invulnerabilidad.
- **El pin de certificado no cubre la re-firma si el atacante también
  regenera el pin**: el pin se genera en build-time del keystore legítimo;
  un atacante que recompila desde el código fuente público puede generar su
  propio pin — pero entonces está construyendo su propio producto, no
  pirateando el distribuido. (El repo es público en visibilidad pero el
  código es propietario, todos los derechos reservados; no hay licencia
  open source.)
- **El zip del módulo Magisk es público por diseño** (canal de Ruta B).
  El binding por UID del daemon y el strip de binarios dificultan su
  reutilización, pero no la impiden.
- **No hay ofuscación del código nativo a nivel LLVM** (no hay toolchain
  OLLVM en la VM ni en CI): la ofuscación es a nivel de fuente/build
  (XorString, predicados opacos, control-flow flattening, `-fvisibility=hidden`
  + strip). El código máquina sigue siendo desensamblable; lo que se eleva es
  el coste del análisis estático (`strings` ya no revela qué se comprueba).
  Un ofuscador LLVM (OLLVM) requeriría un toolchain NDK con pases
  personalizados, no disponible aquí.
- **El cifrado de strings no es cifrado real**: la clave XOR está en el
  binario (en el propio `XorString`/`SecStrings`). Un atacante que analiza
  el flujo puede recuperar los literales. El objetivo es quitar los
  literales de `.rodata` y de la tabla de strings del dex, no hacerlos
  irrecuperables. Contra análisis dinámico (depurador viendo el buffer en
  claro) no protege.
- **La verificación del efecto por el daemon confía en el archivo de hash
  del módulo**: si el atacante controla el módulo completo (puede
  reescribir `ivanna_effect_hash.txt` junto con la `.so`), la verificación
  pasa. El valor real es detectar la sustitución de la `.so` sola (p. ej.
  un módulo legítimo con la `.so` reemplazada por una copia modificada).
  La verificación completa del módulo (firma del zip) no existe.
- **El gate de `CommandServer` usa un símbolo débil**: `effect_integrity_ok()`
  se declara `__attribute__((weak))` en `command_server.cpp` porque los
  tests host compilan ese archivo sin `effect_integrity.cpp` (no se toca
  `tests/CMakeLists.txt`). En el daemon el símbolo siempre está presente
  (`ivanna_daemon.cpp` llama a `effect_integrity_verify()` de forma fuerte,
  así que el daemon no enlaza sin el módulo). En host el gate se omite (no
  hay módulo Magisk en host). Si en el futuro el daemon dejara de llamar a
  `effect_integrity_verify()`, el gate de `SET_*` se desactivaría en
  silencio — mantener la llamada en `main()` es la salvaguarda.
- **No hay ofuscación de flujo de control del dex más allá de R8**: el
  cifrado de strings cubre solo los literales de seguridad de
  `IntegrityGate.kt` (`SecStrings`). El control-flow completo del dex
  requeriría una herramienta comercial (DexGuard), no disponible. No se
  añadió `-repackageclasses` (riesgo sin build Gradle local para verificar).

## Pendiente (después de la fase 2)

- **Gate nativo en el hot path de Ruta A**: inserción de 3 líneas en
  `nativeProcessBlock` (`ivanna_omega_jni.cpp`) + `FusionCore::process`:
  si `!integrity_gate_ok()` → passthrough. NO se hizo en fase 1 ni en fase 2
  porque `ivanna_omega_jni.cpp` lo está editando el agente de Φ y `FusionCore`
  está prohibido tocarlo. (La puerta Kotlin ya bloquea los inits DSP; el gate
  nativo sería defensa en profundidad para el caso de que el DSP se inicie
  por otra vía.)
- **Revisión de R8 en CI**: el primer build release con las reglas nuevas
  debe revisarse por si alguna clase conservada por nombre falta (el
  fallback es restaurar el keep global, pero eso anula la ofuscación).
- **Verificación del build Gradle/R8/NDK completo en CI** (fase 1 y fase 2):
  `generateSoHashes` por variante, el asset en el APK, el hash del efecto en
  el módulo, `IntegrityGate.kt` (Kotlin), y el LTO opt-in si se activa.
- **Ofuscación LLVM (OLLVM)**: si se quiere ofuscación de código nativo a
  nivel de pases (control-flow flattening real, sustitución de
  instrucciones), requiere un toolchain NDK con pases personalizados. No
  disponible en la VM ni en CI hoy.

## Verificación (cómo se probó)

### Fase 1

- Módulo nativo compilado standalone con `g++` (host) + test en `/tmp`:
  vectores conocidos de SHA-256 (cadena vacía, "abc", mensaje largo),
  lógica del gate (false por defecto, set/lockdown), parsing del pin
  (hex válido/inválido, pin vacío → kNoPin, mismatch → kMismatch).
- `generate_build_pins.py` probado con un keystore de prueba generado con
  `keytool` (el SHA-256 extraído coincide con `keytool -list -v`).
- Sintaxis de CMake/Gradle/ProGuard/Kotlin revisada por inspección (no hay
  SDK/NDK en la VM; la CI debe verificar el build del APK).
- Suite host (782/782) NO se re-corrió: los cambios no tocan el código que
  prueban (el módulo de seguridad no está en `tests/CMakeLists.txt`).

### Fase 2 (2026-11-08)

- **Nuevo gtest `tests/test_security_phase2.cpp` (14/14 pasan)**,
  compilado standalone con `g++-14` + gtest precompilado (no está en
  `tests/CMakeLists.txt` — ese archivo no se toca; la CI debe engancharlo):
  XorString round-trip y semillas distintas, predicados opacos
  (siempre-true / siempre-false en [-1000, 1000]), anti-tamper en host
  (sin señales de lockdown; el mask excluye root), helpers testeables
  (parse TracerPid, marcadores de emulador, escaneo de maps, contains_ci),
  SHA-256 por streaming vs one-shot + vectores NIST (`abc`, vacío,
  archivo de 200 KB), archivo inexistente → false, verificación del efecto
  en host (sin módulo → `kNoHashFile`, `effect_integrity_ok()` true).
- **`strings` sobre los objetos compilados** (`-O2 -fvisibility=hidden
  -fstack-protector-strong`): 0 ocurrencias de `frida`, `xposed`,
  `substrate`, `cydia`, `goldfish`, `ranchu`, `IVANNA_SECURITY`,
  `/data/app/`, `libfrida`, `gadget`, `qemu_pipe`, `TracerPid:`,
  `lsposed`, `edxp`, `vbox86`. Hallazgo durante la verificación: el
  constructor `constexpr` de `XorString` NO se plegaba como temporal o
  variable local (el literal en claro se emitía en `.rodata.str1.1`);
  se corrigió con `static constexpr` dentro de una lambda invocada
  (`IVANNA_XSTR`) + inicialización de `data_` en la lista de
  inicialización (sin ella el constructor no es `constexpr` válido).
  Los hits residuales de `emulator` son nombres de símbolo (`.symtab`),
  no literales: tras `strip` son 0 (el build release hace strip).
  Las rutas `/vendor/lib64/soundfx/...` del daemon NO están cifradas por
  diseño (son rutas públicas del sistema, no secretos).
- **Mecanismo weak/strong verificado** con un ejemplo mínimo: con la
  definición fuerte enlazada, la referencia débil resuelve a ella; sin
  ella, el símbolo es nulo y el gate se omite. El build host de
  `test_user_flags_channel` (que compila `command_server.cpp`) falló
  primero con `undefined reference to effect_integrity_ok()` y enlaza
  tras el cambio a símbolo débil; el test pasa (32/32).
- **Suite host completa re-corrida** (los cambios tocan `command_server.cpp`,
  que está en el grafo host): **820/820 pasan, 0 fallos** (la suite creció
  de 782 a 820 por los tests de Φ). `tests/CMakeLists.txt` NO se tocó.
- **Sintaxis verificada** (`g++-14 -fsyntax-only`): `ivanna_daemon.cpp`,
  `command_server.cpp`, `effect_integrity.cpp`, `omega_effect.cpp`
  (solo un warning preexistente de AELI, no relacionado).
- `obfuscate_strings.py --check` confirma que `SecStrings` coincide con
  los literales. `generate_so_hashes.py` probado con una `.so` real
  (el hash coincide con `sha256sum`); falla si la `.so` no existe.
- `build.yml`: YAML válido (parseado). La CI debe verificar el build
  Gradle/R8/NDK completo, `generateSoHashes` por variante, el asset en el
  APK, el hash del efecto en el módulo y la compilación Kotlin (sin
  kotlinc en la VM, `IntegrityGate.kt` se revisó por inspección).

## Historial

- v2.5.369 (2026-11-08): endurecimiento inicial (este documento). Archivos
  nuevos: módulo `security/` (nativo + Kotlin), `tools/security/`,
  `docs/SECURITY_INTERNAL.md`. Modificados: `CMakeLists.txt` (app +
  daemon), `proguard-rules.pro`, `build.gradle.kts`, `.gitignore`,
  `omega_effect.cpp` (self-check Ruta B), `IVANNAApplication.kt`,
  `DSPBridge.kt`, `MainActivity.kt`, `AudioEngine.kt`.
- v2.5.369 fase 2 (2026-11-08): ofuscación y anti-manipulación. Archivos
  nuevos: `security/ivanna_obfuscate.h`, `security/ivanna_antitamper.{h,cpp}`,
  `daemon/core/effect_integrity.{h,cpp}`, `tests/test_security_phase2.cpp`,
  `tools/security/generate_so_hashes.py`,
  `tools/security/obfuscate_strings.py`. Modificados:
  `security/ivanna_integrity.{h,cpp}` (SHA-256 por streaming + hash de
  archivo, literales cifrados), `security/ivanna_integrity_jni.cpp`
  (nativeSha256File, nativeAntiTamperSignals),
  `security/IntegrityGate.kt` (paso 4 hash .so, paso 5 anti-tamper,
  SecStrings), `app/src/main/cpp/CMakeLists.txt` (anti-tamper en ambos
  targets, LTO opt-in), `daemon/CMakeLists.txt` (integrity +
  effect_integrity), `daemon/ivanna_daemon.cpp` (verificación en arranque),
  `daemon/control/command_server.cpp` (gate SET_* por símbolo débil),
  `omega_effect.cpp` (anti-tamper en Ruta B), `app/build.gradle.kts`
  (generateSoHashes por variante), `.gitignore` (asset generado),
  `.github/workflows/build.yml` (hash del efecto + validación del asset
  en el APK). Sin push, sin tags. `tests/CMakeLists.txt` NO se tocó.
