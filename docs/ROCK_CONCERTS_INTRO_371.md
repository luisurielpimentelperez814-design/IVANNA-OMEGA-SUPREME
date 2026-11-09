# Colección de conciertos de rock + intro de apertura — v2.5.371

Petición directa de Esse (2026-11-09 02:48 CST), lo último para la 371:
(1) una recolección de los mejores conciertos de rock, y
(2) que vuelva la intro de apertura de la APK — que cuando se instale,
comience el video de intro.

## Parte 1 — Colección de los mejores conciertos de rock

### Archivo creado

`app/src/main/java/com/ivanna/omega/music/RockConcertsCollection.kt`
(nuevo paquete `com.ivanna.omega.music`; no existía directorio `music/`).

- **30 conciertos** curados (artista, título, recinto, ciudad, año, género,
  por qué es legendario en 2–3 frases, servicios reales donde escucharlo/verlo,
  título exacto para buscar). Sin URLs inventadas: solo servicios reales
  (YouTube, Spotify, TIDAL, Amazon Music) + `searchTitle`.
- API: `all`, `byGenre(genre)`, `byId(id)`, `byYear(year)`,
  `featuredOfTheDay(date)` (rotación determinista por día del año: el mismo
  concierto todo el día, otro al día siguiente; la colección completa rota
  cada mes), `genresPresent()`.
- Géneros canónicos en `RockConcertGenres` (11 valores).

### Los 30

| # | Artista | Concierto | Año | Género |
|---|---------|-----------|-----|--------|
| 1 | Queen | Live Aid, Wembley | 1985 | Hard Rock |
| 2 | Pink Floyd | Live at Pompeii | 1972 | Rock progresivo |
| 3 | Led Zeppelin | Royal Albert Hall | 1970 | Hard Rock |
| 4 | Led Zeppelin | Knebworth | 1979 | Hard Rock |
| 5 | Metallica | Monsters of Rock, Moscú | 1991 | Thrash Metal |
| 6 | Pantera | Monsters of Rock, Moscú | 1991 | Groove Metal |
| 7 | AC/DC | Live at Donington | 1991 | Hard Rock |
| 8 | Nirvana | MTV Unplugged in New York | 1993 | Grunge acústico |
| 9 | Guns N' Roses | Live at the Ritz | 1988 | Hard Rock |
| 10 | Black Sabbath | Live in Paris (L'Olympia) | 1970 | Heavy Metal |
| 11 | Iron Maiden | Rock in Rio | 2001 | Heavy Metal |
| 12 | Deep Purple | Made in Japan | 1972 | Hard Rock |
| 13 | The Rolling Stones | Hyde Park | 1969 | Rock |
| 14 | Rush | Exit... Stage Left | 1981 | Rock progresivo |
| 15 | Dream Theater | Live at Budokan | 2004 | Metal progresivo |
| 16 | Tool | Salival (en vivo) | 2000 | Metal progresivo |
| 17 | Rammstein | Live in Paris | 2012 | Metal industrial |
| 18 | Pink Floyd | Pulse, Earls Court | 1994 | Rock progresivo |
| 19 | Queen | Live at Wembley '86 | 1986 | Hard Rock |
| 20 | The Who | Live at Leeds | 1970 | Hard Rock |
| 21 | Jimi Hendrix | Woodstock | 1969 | Rock psicodélico |
| 22 | U2 | Live at Red Rocks | 1983 | Rock |
| 23 | Dire Straits | Alchemy Live | 1983 | Rock |
| 24 | Alice in Chains | MTV Unplugged | 1996 | Grunge acústico |
| 25 | Pearl Jam | MTV Unplugged | 1992 | Grunge acústico |
| 26 | Rainbow (Dio) | Live in Munich 1977 | 1977 | Heavy Metal |
| 27 | Scorpions | Tokyo Tapes | 1978 | Hard Rock |
| 28 | Judas Priest | Unleashed in the East | 1979 | Heavy Metal |
| 29 | Motörhead | No Sleep 'til Hammersmith | 1981 | Speed Metal |
| 30 | Van Halen | US Festival | 1983 | Hard Rock |

### Punto de integración UI (sin tocar la UI existente)

Nueva pantalla `RockConcertsScreen` (archivo nuevo, p. ej.
`app/src/main/java/com/ivanna/omega/ui/RockConcertsScreen.kt`):

- `LazyColumn`: tarjeta del destacado del día arriba
  (`RockConcertsCollection.featuredOfTheDay()`), chips de filtro por género
  (`RockConcertGenres.all` / `genresPresent()`), y lista completa.
- Cada tarjeta: artista, título, recinto + ciudad + año, género, el texto
  de `whyLegendary`, y los servicios de `listenOn` con el `searchTitle`
  (al tocar, abrir el buscador del servicio o copiar el título).
- Nueva ruta en el NavHost de `MainActivity.kt` (líneas ~355–380):
  `composable("rock_concerts") { RockConcertsScreen(onBack = { nav.popBackStack() }) }`
  y añadir la constante en `ui/IvannaRoute.kt`:
  `const val ROCK_CONCERTS = "rock_concerts"`.
- Acceso sugerido: un botón/tarjeta "CONCIERTOS LEGENDARIOS" en el
  dashboard o en la pantalla `IntroScreen` (el selector de bandas, que ya
  lista Grand Funk Railroad, Led Zeppelin, Rush, Budgie… — el mismo
  público). También puede enlazarse desde la ruta huérfana reparada
  `music_intelligence`, que ya existe en el NavHost.

## Parte 2 — Intro de apertura de la APK

### Estado: LA INTRO EXISTE Y ESTÁ CABLEADA (no hay que reconstruirla)

- **Video:** `app/src/main/res/raw/ivanna_intro.mp4` — 5.617.268 B,
  720×1280, H.264 + AAC, **10,0 s** exactos (la duración ideal para una
  intro; el plan sugería 10–15 s y el video ya la cumple).
- **Cableado (MainActivity.kt, líneas 355–360 y 929–976, idéntico en la
  369 y en HEAD):** NavHost `splash -> introVideo -> intro -> dashboard`.
  `IntroVideoScreen` usa VideoView nativo con
  `android.resource://<paquete>/R.raw.ivanna_intro`, auto-salto al
  completar, botón SALTAR, y salto automático si el video da error.
- **Historial:** creada en el commit `89f784e0` (2026-09-14, "video intro
  tras splash y antes de permisos — petición directa del usuario").
  Ese commit NO es ancestro de HEAD ni de la 369 (la historia se
  reescribió), pero el código se re-aplicó en la línea actual y la 369
  (99cae296) SÍ contiene el video y el cableado (verificado con
  `git show 99cae296`).

### Diagnóstico: por qué Esse no la ve al instalar

El video existe y está cableado, pero NO es lo primero que se ve. El flujo
actual es:

1. `splash` — aviso legal + botón **"ACEPTAR E INICIAR"** (hay que tocarlo).
2. `introVideo` — recién aquí arranca el video (10 s).
3. `intro` — selector de banda.
4. `dashboard`.

Al instalar, lo primero que aparece es el texto legal, no el video. Y hay
un segundo problema silencioso: `IntroVideoScreen` tiene
`setOnErrorListener { _, _, _ -> onContinue(); true }` — si el VideoView
falla al decodificar, salta al selector de banda **sin mostrar nada**,
sin mensaje. Desde fuera eso se ve exactamente como "la intro desapareció".

### El fix: el video, primero (archivo nuevo + cambio de 3 líneas)

**Archivo nuevo (ya creado):**
`app/src/main/java/com/ivanna/omega/ui/IntroVideoGate.kt` — puerta de
primer arranque con SharedPreferences (`ivanna_intro_video`): el estado
sobrevive a reinicios de la app pero se borra al desinstalar (al
reinstalar, el video vuelve a ser lo primero, como pidió Esse). API:
`hasShownIntroVideo()`, `shouldShowIntroVideoFirst()`,
`markIntroVideoShown()`, `resetIntroVideo()` ("ver intro de nuevo").

**Cambio exacto en MainActivity.kt (NO aplicado aquí porque el subagente
de la 371 está tocando ese archivo; aplicar al integrar la 371):**

```kotlin
// Antes (líneas 355–360):
NavHost(nav, startDestination = "splash") {
    composable("splash") { SplashScreen { nav.navigate("introVideo") } }
    composable("introVideo") {
        IntroVideoScreen {
            nav.navigate("intro") { popUpTo("introVideo") { inclusive = true } }
        }
    }

// Después — el video es LO PRIMERO al instalar; el aviso legal va después:
val introAlreadyShown = remember { IntroVideoGate.hasShownIntroVideo(context) }
NavHost(nav, startDestination = if (introAlreadyShown) "splash" else "introVideo") {
    composable("splash") {
        SplashScreen {
            nav.navigate(if (IntroVideoGate.hasShownIntroVideo(context)) "intro" else "introVideo")
        }
    }
    composable("introVideo") {
        IntroVideoScreen {
            IntroVideoGate.markIntroVideoShown(context)
            nav.navigate("splash") { popUpTo("introVideo") { inclusive = true } }
        }
    }
```

Flujo resultante en el primer arranque tras instalar:
`introVideo` (10 s, SALTAR) → `splash` (aviso legal, ACEPTAR) →
`intro` (banda) → `dashboard`. En arranques posteriores:
`splash` → `intro` → `dashboard` (el video no se repite).

**Endurecimiento recomendado (opcional, en el mismo cambio):** en
`IntroVideoScreen`, si el video da error, mostrar el logo + un texto
"Toca para continuar" en vez de saltar en silencio, para que un fallo de
decodificación nunca se vea como "intro desaparecida".

### Video promocional del workspace: NO reutilizar como intro

`~/workspace/ivanna-promo/ivanna-omega-supreme-promo.mp4` — 20,04 s,
720×1280, 17,7 MB. Es el video de promoción (marketing), no una intro:
dura el doble que el video de intro actual (10 s) y pesaría 3× más dentro
del APK. La intro correcta es la que ya existe (`ivanna_intro.mp4`,
10 s, 5,6 MB). Si algún día se quiere renovar la intro, el promo se
puede recortar a ~10 s, pero no hace falta para la 371.

## Veredicto para la 371

- **Colección de rock: LISTA.** Archivo creado y verificado (30 conciertos,
  llaves balanceadas). Solo falta la pantalla UI + la ruta (trabajo nuevo,
  no modifica nada existente) — entra en la 371.
- **Intro: LISTA para aplicar.** Video y cableado ya existen; el helper
  `IntroVideoGate.kt` está creado; solo falta el cambio de 3 líneas en
  MainActivity.kt (documentado arriba, con el código exacto). Entra en
  la 371 en cuanto el subagente de la 371 aplique el cambio.
- **Compilación Kotlin:** sin kotlinc/Android SDK en la VM; la CI debe
  compilar los dos archivos nuevos al pushear la 371.
