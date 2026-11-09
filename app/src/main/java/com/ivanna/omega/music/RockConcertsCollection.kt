package com.ivanna.omega.music

import java.time.LocalDate

/**
 * RockConcertsCollection — la colección curada de los mejores conciertos de
 * rock y metal de la historia, para IVANNA OMEGA SUPREME (v2.5.371).
 *
 * Criterio de selección: calidad de sonido, interpretación y leyenda. Cada
 * entrada trae dónde escucharla/verla: solo servicios reales (YouTube,
 * Spotify, TIDAL, Amazon Music, Apple Music). NO se guardan URLs: el
 * [searchTitle] es el título exacto para buscar en esos servicios, porque
 * los enlaces cambian y se rompen; el título no.
 *
 * Uso:
 * - [RockConcertsCollection.all]: la colección completa (30 conciertos).
 * - [RockConcertsCollection.byGenre]: filtra por género.
 * - [RockConcertsCollection.featuredOfTheDay]: concierto destacado del día,
 *   rotación determinista por día del año (el mismo concierto todo el día,
 *   otro al día siguiente).
 * - [RockConcertsCollection.byId]: busca por id.
 *
 * Integración UI (documentada en docs/ROCK_CONCERTS_INTRO_371.md): nueva
 * ruta "rock_concerts" en el NavHost de MainActivity, pantalla
 * RockConcertsScreen (LazyColumn de tarjetas + filtro por género +
 * destacado del día arriba). Sin tocar la UI existente.
 */
data class RockConcert(
    /** Id estable, kebab-case: "queen-live-aid-1985". */
    val id: String,
    val artist: String,
    /** Nombre del concierto / álbum en vivo. */
    val title: String,
    val venue: String,
    val city: String,
    val year: Int,
    /** Género canónico de [RockConcertGenres]. */
    val genre: String,
    /** Por qué es legendario (2–3 frases). */
    val whyLegendary: String,
    /** Servicios reales donde existe (YouTube, Spotify, TIDAL…). */
    val listenOn: List<String>,
    /** Título exacto para buscar en esos servicios. */
    val searchTitle: String
)

object RockConcertGenres {
    const val HARD_ROCK = "Hard Rock"
    const val HEAVY_METAL = "Heavy Metal"
    const val THRASH_METAL = "Thrash Metal"
    const val GROOVE_METAL = "Groove Metal"
    const val PROGRESSIVE_ROCK = "Rock progresivo"
    const val PROGRESSIVE_METAL = "Metal progresivo"
    const val GRUNGE_ACOUSTIC = "Grunge acústico"
    const val ROCK = "Rock"
    const val PSYCHEDELIC_ROCK = "Rock psicodélico"
    const val INDUSTRIAL_METAL = "Metal industrial"
    const val SPEED_METAL = "Speed Metal"

    val all: List<String> = listOf(
        HARD_ROCK, HEAVY_METAL, THRASH_METAL, GROOVE_METAL,
        PROGRESSIVE_ROCK, PROGRESSIVE_METAL, GRUNGE_ACOUSTIC,
        ROCK, PSYCHEDELIC_ROCK, INDUSTRIAL_METAL, SPEED_METAL
    )
}

object RockConcertsCollection {

    val all: List<RockConcert> = listOf(
        RockConcert(
            id = "queen-live-aid-1985",
            artist = "Queen",
            title = "Live Aid",
            venue = "Wembley Stadium",
            city = "Londres",
            year = 1985,
            genre = RockConcertGenres.HARD_ROCK,
            whyLegendary = "Veinte minutos que la historia del rock declaró perfectos: " +
                "Freddie Mercury dirigiendo a 72.000 personas como un director de orquesta, " +
                "con 'Radio Ga Ga' coreada por todo el estadio. El concierto más citado " +
                "como 'el mejor de la historia' en encuestas de músicos y público.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Queen Live Aid Wembley 1985"
        ),
        RockConcert(
            id = "pink-floyd-pompeii-1972",
            artist = "Pink Floyd",
            title = "Live at Pompeii",
            venue = "Anfiteatro de Pompeya",
            city = "Pompeya",
            year = 1972,
            genre = RockConcertGenres.PROGRESSIVE_ROCK,
            whyLegendary = "Pink Floyd tocando entre ruinas romanas de 2.000 años, sin público, " +
                "solo cámaras y el eco de la piedra. 'Echoes' en ese anfiteatro vacío es el " +
                "momento más cinematográfico del rock progresivo: el sonido como arqueología.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Pink Floyd Live at Pompeii 1972"
        ),
        RockConcert(
            id = "led-zeppelin-royal-albert-hall-1970",
            artist = "Led Zeppelin",
            title = "Royal Albert Hall",
            venue = "Royal Albert Hall",
            city = "Londres",
            year = 1970,
            genre = RockConcertGenres.HARD_ROCK,
            whyLegendary = "El Zeppelin joven, en su punto más feroz: Page con el arco de violín " +
                "en 'Dazed and Confused', Plant en plena garganta, Bonham demoliendo la batería. " +
                "La BBC lo filmó casi completo y el documento quedó como la prueba definitiva " +
                "de que eran la mejor banda en vivo del planeta.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Led Zeppelin Royal Albert Hall 1970"
        ),
        RockConcert(
            id = "led-zeppelin-knebworth-1979",
            artist = "Led Zeppelin",
            title = "Knebworth",
            venue = "Knebworth House",
            city = "Knebworth",
            year = 1979,
            genre = RockConcertGenres.HARD_ROCK,
            whyLegendary = "El regreso triunfal tras dos años sin tocar en Inglaterra, ante " +
                "200.000 personas en dos noches. Tres horas de set con 'Achilles Last Stand' " +
                "y 'Kashmir' sonando como un ejército. El último gran concierto británico " +
                "de la banda antes de la muerte de Bonham.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Led Zeppelin Knebworth 1979"
        ),
        RockConcert(
            id = "metallica-moscu-1991",
            artist = "Metallica",
            title = "Monsters of Rock — Moscú",
            venue = "Aeródromo de Tushino",
            city = "Moscú",
            year = 1991,
            genre = RockConcertGenres.THRASH_METAL,
            whyLegendary = "Más de un millón de personas — la mayor multitud de un concierto de " +
                "rock en la historia — celebrando la caída del telón de acero con 'Enter Sandman'. " +
                "El ejército soviético custodiando el escenario mientras el público se subía a " +
                "los tanques: una imagen imposible de repetir.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Metallica Live Moscow 1991 Monsters of Rock"
        ),
        RockConcert(
            id = "pantera-moscu-1991",
            artist = "Pantera",
            title = "Monsters of Rock — Moscú",
            venue = "Aeródromo de Tushino",
            city = "Moscú",
            year = 1991,
            genre = RockConcertGenres.GROOVE_METAL,
            whyLegendary = "El mismo escenario histórico que Metallica, pero con el groove más " +
                "pesado del planeta: Dimebag y Phil Anselmo ante un millón de rusos que " +
                "coreaban cada riff. 'Domination' en Moscú es el documento del metal " +
                "conquistando un continente entero en una tarde.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Pantera Live Moscow 1991 Monsters of Rock"
        ),
        RockConcert(
            id = "acdc-donington-1991",
            artist = "AC/DC",
            title = "Live at Donington",
            venue = "Donington Park",
            city = "Donington",
            year = 1991,
            genre = RockConcertGenres.HARD_ROCK,
            whyLegendary = "La gira de 'The Razors Edge' en su punto máximo: Brian Johnson " +
                "en plena forma, Angus Young en uniforme escolar recorriendo la pasarela, y " +
                "los cañones de 'For Those About to Rock' disparando sobre 72.000 personas. " +
                "El concierto de hard rock más limpio y demoledor jamás filmado.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "AC/DC Live at Donington 1991"
        ),
        RockConcert(
            id = "nirvana-unplugged-1993",
            artist = "Nirvana",
            title = "MTV Unplugged in New York",
            venue = "Sony Music Studios",
            city = "Nueva York",
            year = 1993,
            genre = RockConcertGenres.GRUNGE_ACOUSTIC,
            whyLegendary = "Kurt Cobain desenchufado, con velas y lirios sobre el escenario, " +
                "cantando 'Where Did You Sleep Last Night' con una voz que se rompe al final. " +
                "Cinco meses después estaba muerto; este concierto quedó como su testamento. " +
                "El Unplugged más grande de la historia de MTV.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL", "Amazon Music"),
            searchTitle = "Nirvana MTV Unplugged in New York 1993"
        ),
        RockConcert(
            id = "guns-n-roses-ritz-1988",
            artist = "Guns N' Roses",
            title = "Live at the Ritz",
            venue = "The Ritz",
            city = "Nueva York",
            year = 1988,
            genre = RockConcertGenres.HARD_ROCK,
            whyLegendary = "Los Guns antes de la fama descontrolada: Axl, Slash, Izzy, Duff y " +
                "Steven en un club pequeño, sudando 'Appetite for Destruction' canción por " +
                "canción. Crudo, peligroso y perfecto. El concierto que demuestra que el " +
                "mejor hard rock se toca de cerca, no en estadios.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Guns N' Roses Live at the Ritz 1988"
        ),
        RockConcert(
            id = "black-sabbath-paris-1970",
            artist = "Black Sabbath",
            title = "Live in Paris",
            venue = "L'Olympia",
            city = "París",
            year = 1970,
            genre = RockConcertGenres.HEAVY_METAL,
            whyLegendary = "El heavy metal naciendo en directo: Ozzy, Tony Iommi, Geezer y Bill " +
                "Ward tocando 'Paranoid', 'War Pigs' e 'Iron Man' meses después de grabarlos. " +
                "El riff de Iommi en París 1970 es el acta de nacimiento del metal, " +
                "interpretada por sus inventores.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Black Sabbath Live Paris 1970 Olympia"
        ),
        RockConcert(
            id = "iron-maiden-rock-in-rio-2001",
            artist = "Iron Maiden",
            title = "Rock in Rio",
            venue = "Cidade do Rock",
            city = "Río de Janeiro",
            year = 2001,
            genre = RockConcertGenres.HEAVY_METAL,
            whyLegendary = "Bruce Dickinson de vuelta en la banda, 250.000 brasileños coreando " +
                "cada verso de 'Fear of the Dark' y 'The Trooper'. La mejor noche de la " +
                "reunión de Maiden y el concierto de metal más grande jamás grabado en " +
                "Sudamérica. Eddie gigante sobre el escenario, como debe ser.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Iron Maiden Rock in Rio 2001"
        ),
        RockConcert(
            id = "deep-purple-made-in-japan-1972",
            artist = "Deep Purple",
            title = "Made in Japan",
            venue = "Festival Hall / Budokan",
            city = "Osaka / Tokio",
            year = 1972,
            genre = RockConcertGenres.HARD_ROCK,
            whyLegendary = "El álbum en vivo más influyente de la historia: 'Smoke on the Water', " +
                "'Highway Star' y 'Child in Time' con la formación Mk II en estado de gracia. " +
                "El público japonés en silencio absoluto entre canciones y explotando en cada " +
                "riff. La referencia de sonido en vivo que todos los demás intentan igualar.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL", "Amazon Music"),
            searchTitle = "Deep Purple Made in Japan 1972"
        ),
        RockConcert(
            id = "rolling-stones-hyde-park-1969",
            artist = "The Rolling Stones",
            title = "Hyde Park",
            venue = "Hyde Park",
            city = "Londres",
            year = 1969,
            genre = RockConcertGenres.ROCK,
            whyLegendary = "El concierto gratuito ante 250.000 personas que presentó a Mick Taylor " +
                "y despidió a Brian Jones — Jagger leyó un poema de Shelley en su memoria y " +
                "soltaron miles de mariposas blancas. 'Sympathy for the Devil' al atardecer " +
                "londinense: el rock como ritual público.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Rolling Stones Hyde Park 1969"
        ),
        RockConcert(
            id = "rush-exit-stage-left-1981",
            artist = "Rush",
            title = "Exit... Stage Left",
            venue = "The Forum / Maple Leaf Gardens",
            city = "Montreal / Toronto",
            year = 1981,
            genre = RockConcertGenres.PROGRESSIVE_ROCK,
            whyLegendary = "Tres músicos sonando como seis: Neil Peart, Geddy Lee y Alex Lifeson " +
                "ejecutando 'YYZ', 'Tom Sawyer' y 'La Villa Strangiato' con precisión de " +
                "relojero y furia de estadio. El documento definitivo del rock progresivo " +
                "en vivo: complejidad extrema sin perder un gramo de pegada.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Rush Exit Stage Left 1981"
        ),
        RockConcert(
            id = "dream-theater-budokan-2004",
            artist = "Dream Theater",
            title = "Live at Budokan",
            venue = "Nippon Budokan",
            city = "Tokio",
            year = 2004,
            genre = RockConcertGenres.PROGRESSIVE_METAL,
            whyLegendary = "Tres horas de metal progresivo en el templo sagrado del rock japonés: " +
                "'Metropolis Pt. 1', 'The Glass Prison' y un 'In the Name of God' que pone " +
                "la piel de gallina. La banda en su cima técnica, con el público de Tokio " +
                "coreando cada compás impar como si fuera un himno.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Dream Theater Live at Budokan 2004"
        ),
        RockConcert(
            id = "tool-salival-2000",
            artist = "Tool",
            title = "Salival (en vivo)",
            venue = "Varios recintos",
            city = "Gira 1992–1998",
            year = 2000,
            genre = RockConcertGenres.PROGRESSIVE_METAL,
            whyLegendary = "El único documento oficial en vivo de Tool durante décadas: versiones " +
                "extendidas de 'Pushit', 'Stinkfist' y 'Third Eye' con Maynard en las sombras " +
                "y Danny Carey demostrando por qué es el mejor baterista del metal moderno. " +
                "Oscuro, hipnótico y matemático a la vez.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Tool Salival live"
        ),
        RockConcert(
            id = "rammstein-paris-2012",
            artist = "Rammstein",
            title = "Live in Paris",
            venue = "AccorHotels Arena",
            city = "París",
            year = 2012,
            genre = RockConcertGenres.INDUSTRIAL_METAL,
            whyLegendary = "Fuego, pirotecnia y precisión alemana: 'Du Hast', 'Ich Will' y " +
                "'Sonne' con un espectáculo que convierte cada canción en una ópera " +
                "industrial. Filmado en cine, es el concierto de metal mejor producido " +
                "visualmente jamás estrenado.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Rammstein Live in Paris 2012"
        ),
        RockConcert(
            id = "pink-floyd-pulse-1994",
            artist = "Pink Floyd",
            title = "Pulse",
            venue = "Earls Court",
            city = "Londres",
            year = 1994,
            genre = RockConcertGenres.PROGRESSIVE_ROCK,
            whyLegendary = "'The Dark Side of the Moon' completo, en vivo, con un cerdo inflable " +
                "volando sobre el estadio y un láser que partía el cielo. Gilmour en " +
                "'Comfortably Numb' toca el solo más famoso de la historia del rock. " +
                "El concierto más espectacular de Pink Floyd, grabado en su última gira.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Pink Floyd Pulse Earls Court 1994"
        ),
        RockConcert(
            id = "queen-wembley-1986",
            artist = "Queen",
            title = "Live at Wembley '86",
            venue = "Wembley Stadium",
            city = "Londres",
            year = 1986,
            genre = RockConcertGenres.HARD_ROCK,
            whyLegendary = "El concierto completo que Live Aid solo anticipó: dos noches, 72.000 " +
                "personas por noche, y Freddie Mercury en su cima absoluta — la llamada y " +
                "respuesta vocal con el público en 'Love of My Life' es el momento más " +
                "emocionante jamás grabado en un estadio.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Queen Live at Wembley 1986"
        ),
        RockConcert(
            id = "the-who-leeds-1970",
            artist = "The Who",
            title = "Live at Leeds",
            venue = "Universidad de Leeds",
            city = "Leeds",
            year = 1970,
            genre = RockConcertGenres.HARD_ROCK,
            whyLegendary = "El álbum en vivo que todos los demás álbumes en vivo intentan ser: " +
                "Townshend destrozando la guitarra, Keith Moon tocando como si el mundo se " +
                "acabara y una versión de 15 minutos de 'My Generation' que es pura " +
                "electricidad. Grabado en una universidad, no en un estadio: el rock en su " +
                "estado más crudo y real.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL", "Amazon Music"),
            searchTitle = "The Who Live at Leeds 1970"
        ),
        RockConcert(
            id = "jimi-hendrix-woodstock-1969",
            artist = "Jimi Hendrix",
            title = "Woodstock",
            venue = "Granja de Max Yasgur",
            city = "Bethel",
            year = 1969,
            genre = RockConcertGenres.PSYCHEDELIC_ROCK,
            whyLegendary = "El lunes por la mañana, ante un campo casi vacío y embarrado, Hendrix " +
                "tocó el himno de Estados Unidos con su Stratocaster y lo convirtió en el " +
                "grito de una generación contra la guerra. Tres minutos de guitarra que " +
                "resumen toda la década de los sesenta.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Jimi Hendrix Woodstock 1969 Star Spangled Banner"
        ),
        RockConcert(
            id = "u2-red-rocks-1983",
            artist = "U2",
            title = "Live at Red Rocks",
            venue = "Anfiteatro Red Rocks",
            city = "Colorado",
            year = 1983,
            genre = RockConcertGenres.ROCK,
            whyLegendary = "Bajo la lluvia, entre rocas rojas milenarias, un Bono de 23 años " +
                "ondeando una bandera blanca en 'Sunday Bloody Sunday'. El video que convirtió " +
                "a U2 de banda prometedora en leyenda. El escenario natural más hermoso " +
                "jamás usado para un concierto de rock.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "U2 Live at Red Rocks 1983"
        ),
        RockConcert(
            id = "dire-straits-alchemy-1983",
            artist = "Dire Straits",
            title = "Alchemy Live",
            venue = "Hammersmith Odeon",
            city = "Londres",
            year = 1983,
            genre = RockConcertGenres.ROCK,
            whyLegendary = "Mark Knopfler y su Stratocaster roja en 'Sultans of Swing' y " +
                "'Telegraph Road': el sonido de guitarra más limpio y expresivo del rock. " +
                "Dos noches en el Hammersmith que quedaron como el estándar de cómo debe " +
                "sonar una banda en vivo: cada nota en su sitio, nada de más.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Dire Straits Alchemy Live 1983"
        ),
        RockConcert(
            id = "alice-in-chains-unplugged-1996",
            artist = "Alice in Chains",
            title = "MTV Unplugged",
            venue = "Brooklyn Academy of Music",
            city = "Nueva York",
            year = 1996,
            genre = RockConcertGenres.GRUNGE_ACOUSTIC,
            whyLegendary = "Layne Staley, frágil y luminoso, cantando 'Down in a Hole' y 'Would?' " +
                "con una voz que corta el aire. La banda no tocaba junta desde hacía tres " +
                "años y aun así sonó perfecta. El Unplugged más oscuro y hermoso de MTV, " +
                "y la última gran noche de Layne.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Alice in Chains MTV Unplugged 1996"
        ),
        RockConcert(
            id = "pearl-jam-unplugged-1992",
            artist = "Pearl Jam",
            title = "MTV Unplugged",
            venue = "Kaufman Astoria Studios",
            city = "Nueva York",
            year = 1992,
            genre = RockConcertGenres.GRUNGE_ACOUSTIC,
            whyLegendary = "Eddie Vedder descalzo, escribiendo 'Porch' en su brazo y cantando " +
                "'Black' con una intensidad que dejó al estudio en silencio. El grunge " +
                "en su momento más íntimo: sin distorsión, sin luces, solo canciones " +
                "que ya eran himnos con apenas un año de vida.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Pearl Jam MTV Unplugged 1992"
        ),
        RockConcert(
            id = "rainbow-munich-1977",
            artist = "Rainbow",
            title = "Live in Munich 1977",
            venue = "Olympiahalle",
            city = "Múnich",
            year = 1977,
            genre = RockConcertGenres.HEAVY_METAL,
            whyLegendary = "Ronnie James Dio en su cima, con Ritchie Blackmore dirigiendo una " +
                "banda que sonaba como una orquesta eléctrica: 'Stargazer' y 'Catch the " +
                "Rainbow' en Múnich son el heavy metal antes de que existiera el nombre. " +
                "La voz de Dio en este concierto es la referencia de todas las voces del metal.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Rainbow Live in Munich 1977 Dio"
        ),
        RockConcert(
            id = "scorpions-tokyo-tapes-1978",
            artist = "Scorpions",
            title = "Tokyo Tapes",
            venue = "Nakano Sun Plaza",
            city = "Tokio",
            year = 1978,
            genre = RockConcertGenres.HARD_ROCK,
            whyLegendary = "Los Scorpions con Uli Jon Roth, antes del estrellato americano: " +
                "'In Trance', 'Fly to the Rainbow' y 'We'll Burn the Sky' tocadas con " +
                "una elegancia que el hard rock rara vez alcanza. El público japonés, " +
                "enloquecido, convirtió estas noches en el álbum en vivo más querido de " +
                "la banda. La guitarra de Roth aquí es de otro planeta.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Scorpions Tokyo Tapes 1978"
        ),
        RockConcert(
            id = "judas-priest-unleashed-in-the-east-1979",
            artist = "Judas Priest",
            title = "Unleashed in the East",
            venue = "Kosei Nenkin Kaikan",
            city = "Tokio",
            year = 1979,
            genre = RockConcertGenres.HEAVY_METAL,
            whyLegendary = "Rob Halford, cuero y tachuelas, cantando 'Victim of Changes' y " +
                "'The Green Manalishi' con un registro que nadie había oído antes en el " +
                "metal. Grabado en Tokio, este disco definió el sonido, la imagen y la " +
                "actitud del heavy metal para la década siguiente.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Judas Priest Unleashed in the East 1979"
        ),
        RockConcert(
            id = "motorhead-hammersmith-1981",
            artist = "Motörhead",
            title = "No Sleep 'til Hammersmith",
            venue = "Hammersmith Odeon",
            city = "Londres",
            year = 1981,
            genre = RockConcertGenres.SPEED_METAL,
            whyLegendary = "Lemmy lo resumió: 'todo más alto que todo lo demás'. El álbum en " +
                "vivo número 1 en Inglaterra, con 'Ace of Spades', 'Overkill' y 'Bomber' " +
                "tocadas a una velocidad que inventó el speed metal y el thrash. El " +
                "concierto más ruidoso y honesto de la historia del rock.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Motorhead No Sleep til Hammersmith 1981"
        ),
        RockConcert(
            id = "van-halen-us-festival-1983",
            artist = "Van Halen",
            title = "US Festival",
            venue = "Glen Helen Regional Park",
            city = "San Bernardino",
            year = 1983,
            genre = RockConcertGenres.HARD_ROCK,
            whyLegendary = "El día más caluroso del festival, con 400.000 personas y Van Halen " +
                "cobrando el caché más alto de la historia hasta entonces. David Lee Roth " +
                "enloquecido, Eddie Van Halen reinventando la guitarra en 'Eruption' y un " +
                "set que cerró el festival más grande de los ochenta. Puro exceso americano.",
            listenOn = listOf("YouTube", "Spotify", "TIDAL"),
            searchTitle = "Van Halen US Festival 1983"
        )
    )

    /** Filtra la colección por género (ver [RockConcertGenres]). */
    fun byGenre(genre: String): List<RockConcert> =
        all.filter { it.genre == genre }

    /** Busca un concierto por su [id] estable, o null si no existe. */
    fun byId(id: String): RockConcert? =
        all.firstOrNull { it.id == id }

    /**
     * Concierto destacado del día: rotación determinista por día del año.
     * El mismo concierto durante todo el día; otro al día siguiente.
     * Con 30 conciertos, la colección completa rota cada mes.
     */
    fun featuredOfTheDay(date: LocalDate = LocalDate.now()): RockConcert =
        all[(date.dayOfYear - 1) % all.size]

    /** Todos los géneros presentes en la colección, en orden de aparición. */
    fun genresPresent(): List<String> =
        all.map { it.genre }.distinct()

    /** Conciertos de un año concreto (p. ej. el año de nacimiento del usuario). */
    fun byYear(year: Int): List<RockConcert> =
        all.filter { it.year == year }
}
