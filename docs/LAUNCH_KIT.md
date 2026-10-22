# Kit de lanzamiento — IVANNA OMEGA SUPREME

Material listo para publicar **desde tus propias cuentas**, canal por canal. Está pensado para que el proyecto
sea visto por quien de verdad puede valorarlo (desarrolladores Android, gente de DSP/audio, comunidad Magisk) y
para que el primer contacto sea honesto y verificable. Sin cuentas falsas, sin intercambio de estrellas, sin spam.

## 0. Reglas de juego (legales y de reputación)

1. **Publica con tu identidad y di que eres el autor.** Cada comunidad lo exige o lo valora.
2. **Lee las reglas de autopromoción de cada sitio antes de publicar** (varios subreddits limitan los posts del
   propio autor y piden participar antes en la comunidad). Si una regla cambió, manda la regla.
3. **Un canal por día, no todos a la vez.** Responde cada comentario las primeras horas: eso pesa más que el
   post. Los algoritmos y los moderadores penalizan los lanzamientos masivos simultáneos.
4. **No pidas ni intercambies estrellas, *likes* o seguidores**, ni uses cuentas falsas, bots o servicios de
   engagement: viola los términos de GitHub/YouTube/Reddit y puede costarte las cuentas.
5. **Contacto con terceros (youtubers, ingenieros, empresas): uno a uno, personalizado, corto y sin exigir nada.**
   Nada de mensajes masivos ni de etiquetar a figuras de la industria en publicaciones ajenas.
6. **Afirma solo lo que puedas demostrar** (sección 2). El público técnico premia la honestidad y castiga el humo.

## 1. Activos

| Archivo | Uso |
|---|---|
| `docs/release_media/ivanna_social_preview.png` (1280×640) | **Súbela tú** en *Settings → General → Social preview* (la API de GitHub no permite subirla). Es la tarjeta que se ve al compartir el enlace. |
| `docs/release_media/ivanna_promo_flyer.png` (1080×1350) | Publicaciones verticales: Instagram/LinkedIn/X, post de Comunidad de YouTube, Telegram. |
| `docs/index.html` (GitHub Pages) | Enlace corto «oficial» del proyecto. |
| Pendiente: **vídeo de 60–90 s** | Lo que más convierte. Guion en §4.6. Graba con el DSP **activo** (las capturas actuales muestran *STANDBY*). |

## 2. Mensajes que puedes sostener hoy (con su evidencia)

| Afirmación | Evidencia |
|---|---|
| Coordina los módulos DSP midiendo el resultado real (original vs. salida) | `include/acoustic_unity_engine.hpp`, `test_unity_closed_loop` |
| 245 pruebas de host en verde, ASan/UBSan, 7 puertas estáticas en CI | `telemetry/ctest_summary.json`, pestaña *Actions* |
| Verificación de tiempo real (sin `malloc`/locks en el hilo de audio) | `scripts/check_rt_safety.py` |
| Dos rutas: sin root (en la app) y con root (Magisk + daemon) | `README.md` §2 |
| Los controles de usuario llegan de la UI al audio del sistema (Ruta B) | `test_user_flags_channel` (CommandServer real) |

**Evita** (no está medido en dispositivo o no se puede comparar de forma justa): «mejor que Dolby/Dirac», cifras de
mejora de calidad percibida, «el mejor motor de Android». Di en cambio: *medido en host con señales sintéticas;
falta validación en dispositivos reales — por eso busco probadores.* Esa franqueza es tu mejor gancho.

## 3. Calendario sugerido (escalonado)

| Día | Acción |
|---|---|
| −3 | Confirmar el aviso de propiedad intelectual y las licencias de datasets (`THIRD_PARTY_NOTICES.md`), restringir la clave de Firebase, subir *Social preview*, grabar el vídeo. |
| 0 | **Show HN** (mañana, hora de EE. UU. este) + publicación en tu LinkedIn/X/Bluesky con el flyer. |
| +1 | **XDA Forums** (hilo en la sección de Magisk/Software) y **r/Magisk** (si sus reglas lo permiten). |
| +2 | **r/androiddev** / **r/androidapps** según reglas; artículo en **Dev.to** (§4.4). |
| +4 | **Vídeo en YouTube** + post de Comunidad con el flyer; pedir a quien reporte un dispositivo permiso para citarlo. |
| +7 | **Hackaday.io**, **Audio Science Review** (foro de DSP/medición) y **Product Hunt** (si ya tienes demo). |
| +14 | Resumen de resultados: dispositivos probados, issues cerrados, siguientes pasos. Es contenido para otra ronda. |

## 4. Textos listos

### 4.1 Show HN (inglés)

**Título (≤ 80 caracteres):** `Show HN: IVANNA – an Android audio engine that measures its own output`

**Primer comentario (publícalo tú justo después):**

> Hi HN — I'm the author. IVANNA is an proprietary commercial audio engine for Android (C++26 native DSP + Kotlin/Compose) with
> two routes: an in-app route that needs no root, and a Magisk module + root daemon that processes system audio.
>
> The idea I'd like feedback on: instead of a chain of independent effects, a coordinator compares the original
> signal with the final output every block (stereo expansion, L/R correlation, crest factor, high-frequency excess)
> and decides how much each stage — widener, room convolution, exciter, compressor — is allowed to apply, so one
> stage doesn't destroy what another built. Every actuator keeps a fidelity floor and recovers when there's headroom.
>
> What is verified: 245 host tests (including ASan/UBSan runs), static gates for real-time safety and for "dead" code
> (everything must be wired UI → JNI → native → DSP), and an end-to-end test of the control plane. What is *not*
> verified yet: listening tests on real devices — the measurements use synthetic signals. That's why I'm looking
> for people willing to try it and file a device report.
>
> Repo: https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME — happy to answer questions about the
> design, the measurements, or the parts I got wrong.

### 4.2 Reddit / XDA (inglés; adapta el título a las reglas de cada sitio)

**Título:** `IVANNA — proprietary Android audio engine (HRTF, room IRs, closed-loop coordination). Commercial licensing inquiries and device testing`

**Cuerpo:**

> I'm the developer. IVANNA runs in-app without root, or system-wide through a Magisk module + daemon. It combines
> real-time DSP, binaural HRTF, measured room impulse responses and a coordinator that measures the output to keep
> stages from fighting each other.
>
> I have 245 host tests and CI gates, but **no on-device listening data yet**, so I'm asking for testers: model,
> Android version, route (A/B), audio output, and what you heard — "no difference" is a useful report too.
> Report form: https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/issues/new/choose
> Repo: https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME
> Disclosure: I'm the author. Magisk module requires root; use at your own risk and keep a backup of your audio config.

### 4.3 Redes cortas (X / Bluesky / Mastodon / LinkedIn) — español

> Construí un motor de audio para Android que **mide su propia salida** antes de decidir cuánto aplica cada módulo
> (ensanche, salas, excitador, compresor). 245 pruebas, ASan/UBSan, producto propietario. Sin root o con Magisk.
> Busco probadores de dispositivos reales 👇 github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME
> (adjunta `ivanna_promo_flyer.png`)

### 4.4 Artículo (Dev.to / Hashnode) — esquema

1. El problema: efectos de audio en cadena que se pelean (ensanche vs. fase, sala vs. claridad, excitador vs. fatiga).
2. Medir el resultado: correlación L/R, factor de cresta, exceso de agudos; presupuestos con piso de fidelidad.
3. Dos rutas y un solo estado: por qué la app solo puede ser *lectora* del bus y cómo viaja un control hasta `audioserver`.
4. Lo que salió mal (auditoría de código muerto: 14 de 16 campos sin consumidor; un doble *ducking* de −24 dB).
5. Cómo se verifica (host, ASan/UBSan, puertas de CI) y qué falta (escucha en dispositivo).

Los puntos 3 y 4 son el contenido que más comparte la comunidad técnica: errores reales, explicados con honestidad.

### 4.5 YouTube — publicación de Comunidad (usa el flyer vertical)

> Estoy construyendo IVANNA, un motor de audio propietario para Android que mide su propia salida para que
> sus efectos no se pisen entre sí. Necesito probadores de dispositivos reales (con o sin root). Si te interesa el
> audio o el desarrollo Android, el repositorio y el formulario de reporte están en el enlace. ¿Qué te gustaría
> que probara primero? 👇

### 4.6 YouTube — vídeo de 60–90 s

**Título:** `Un motor de audio propietario para Android que mide lo que hace`

| Seg. | Imagen | Locución |
|---|---|---|
| 0–8 | Onda + logo | «La mayoría de los efectos de audio se aplican en cadena y nadie comprueba el resultado.» |
| 8–25 | Pantalla de la app con DSP **activo** | «IVANNA compara el audio original con la salida real y decide cuánto aplica cada módulo.» |
| 25–45 | Panel de telemetría «UNITY MEDIDO» | «Mide expansión, fase, cresta y agudos; si algo se pasa, ese módulo cede.» |
| 45–60 | Terminal con `run_ctest.sh` y *Actions* en verde | «245 pruebas, ASan/UBSan y puertas de calidad en cada cambio.» |
| 60–75 | Flyer + enlace | «Falta lo más importante: probarlo en teléfonos reales. Si quieres ayudar, el enlace está abajo.» |

**Descripción:** resumen de 3 líneas + enlace al repositorio + enlace al formulario de reporte + «Soy el autor».
**Etiquetas:** `android audio`, `magisk module`, `dsp`, `hrtf`, `proprietary software`, `audio engine`.

### 4.6.1 Cómo llegar a otros canales (sin spam)

Elige **3–5** canales concretos de Android/audio/DSP cuyo contenido encaje de verdad, mira qué han publicado y
escribe a cada uno **un mensaje individual**: quién eres, qué hace el proyecto en dos frases, qué pides (probar y
opinar, no promocionar) y el enlace. Si no responden, no insistas.

### 4.7 Mensaje individual a ingenieros/investigadores (esquema)

> Hola [nombre]: vi tu trabajo sobre [tema concreto que de verdad conoces]. Estoy desarrollando un motor de audio
> propietario para Android que mide la salida de su cadena DSP para arbitrar entre módulos
> ([enlace]). Me interesaría tu opinión sobre [pregunta concreta, p. ej. los umbrales de fase/correlación]. Sin
> compromiso: si no te encaja, gracias por leer. Cito tu trabajo en `THIRD_PARTY_NOTICES.md`/CITATION si aplica.

## 5. Antes de pulsar «publicar»

- [ ] Licencia elegida y subida (sin ella nadie puede reutilizar legalmente el código).
- [ ] Licencias de datasets confirmadas (issue #59) — pueden limitar la redistribución.
- [ ] Clave de API de Firebase restringida (paquete + SHA-1) y reglas de Firestore/Storage revisadas.
- [ ] PAT de GitHub revocado y secretos rotados si alguno se pegó en un chat.
- [ ] *Social preview* subida; vídeo con el DSP activo; README sin cifras que no puedas respaldar.
- [ ] Alguien de confianza ha instalado el APK de la última release en un teléfono limpio.

## 6. Medir qué funciona

GitHub → *Insights → Traffic* (visitas y referencias), *Insights → Community*, número de *Reportes de dispositivo*
(la métrica que de verdad importa para este proyecto) y estrellas honestas como consecuencia, no como objetivo.
