/**
 * Curated, versioned acoustic decision knowledge for FotoHolo.
 * This is an engineering rulebook, not a trained model or a replacement for measurement.
 * Keep recommendations conservative, reversible and subordinate to device/user limits.
 */
export const FOTOHOLO_KNOWLEDGE_VERSION = '1.0.0';

export const GENRE_PROFILES = {
  'rock-70s': { label: 'Rock 70s · clásico/orgánico', focus: 'preservar medios de guitarras y voz; dinámica natural; evitar brillo artificial y subgrave exagerado' },
  'rock-80s': { label: 'Rock 80s · arena/synth', focus: 'separación de caja, toms, guitarras y reverbs; controlar agudos fatigantes; mantener pegada sin aplastar transitorios' },
  'rock-90s': { label: 'Rock 90s · alternativo/grunge', focus: 'conservar textura y dinámica de guitarras; evitar medios embarrados; no eliminar la aspereza intencional de la mezcla' },
  'classic-rock': { label: 'Classic rock', focus: 'equilibrio de voz, guitarra y batería; graves firmes sin ocultar bajo eléctrico' },
  'hard-rock-metal': { label: 'Hard rock / metal', focus: 'separar doble bombo, bajo y guitarras densas; limitar fatiga de agudos; no aumentar ganancia automáticamente' },
  'punk': { label: 'Punk / hardcore', focus: 'ataque y energía de medios; conservar velocidad transitoria y evitar compresión adicional innecesaria' },
  'indie-alternative': { label: 'Indie / alternativo', focus: 'mantener timbres y panorama de producción; correcciones suaves, no imponer curva comercial' },
  'pop': { label: 'Pop', focus: 'inteligibilidad vocal, centro estable y graves controlados; no sobrecomprimir' },
  'hip-hop': { label: 'Hip-hop / rap', focus: 'inteligibilidad vocal y kick/bass diferenciados; proteger headroom y no aumentar subgrave sin evidencia' },
  'r-and-b-soul': { label: 'R&B / soul / funk', focus: 'voz presente, bajo articulado y transitorios de percusión; preservar groove y timbre' },
  'jazz': { label: 'Jazz / acústico', focus: 'naturalidad tímbrica, imagen estable y microdinámica; evitar ensanchamiento o reverberación artificial' },
  'classical': { label: 'Clásica / orquestal', focus: 'preservar dinámica, profundidad y colas de sala; no limitar ni elevar loudness de forma automática' },
  'opera-vocal': { label: 'Ópera / vocal', focus: 'inteligibilidad, cuerpo vocal y estabilidad espacial; preservar dinámica y evitar sibilancia' },
  'electronic': { label: 'Electrónica / EDM', focus: 'controlar energía de subgrave, ataque y centro; evitar clipping y acumulación de graves en sala' },
  'house-techno': { label: 'House / techno', focus: 'kick y bajo diferenciados, imagen estable y transitorios; adaptar al sistema sin sumar volumen por defecto' },
  'ambient-downtempo': { label: 'Ambient / downtempo', focus: 'conservar colas, capas y espacio; no confundir reverberación artística con defecto de sala' },
  'reggae-dub': { label: 'Reggae / dub', focus: 'fundamental del bajo legible y delays/reverbs originales preservados; evitar realce indiscriminado de graves' },
  'latin': { label: 'Latina / salsa / merengue / bachata', focus: 'separación de percusión, bajo y voz; preservar síncopa, timbre y dinámica' },
  'regional-mexicano': { label: 'Regional mexicano / banda / norteño', focus: 'inteligibilidad vocal, definición de metales/acordeón y control de aspereza; evitar sobrecargar medios-altos' },
  'reggaeton': { label: 'Reggaetón / urbano', focus: 'kick y bajo controlados, vocal al frente y transitorios limpios; no inferir preferencia por más bass sin permiso' },
  'afrobeats': { label: 'Afrobeats / amapiano', focus: 'groove y percusión definidos, bajo legible y panorama estable; conservar transitorios' },
  'world-folk': { label: 'Folk / world / acústico', focus: 'timbres de instrumentos, voz natural y profundidad; evitar excitación artificial' },
  'gospel-choir': { label: 'Góspel / coro', focus: 'inteligibilidad y anchura sin perder centro; mantener dinámica coral' },
  'podcast-speech': { label: 'Voz / podcast', focus: 'inteligibilidad y baja fatiga; no aplicar expansión espacial agresiva ni ganancia peligrosa' },
  'gaming-cinema': { label: 'Cine / videojuegos', focus: 'direccionalidad coherente y diálogo inteligible; respetar mezcla y límites de reproducción' },
  'unknown': { label: 'Desconocido / automático', focus: 'no asumir género; usar solo mediciones válidas y correcciones mínimas' },
} as const;

export type GenreId = keyof typeof GENRE_PROFILES;

export const VENUE_PROFILES = {
  'headphones': { label: 'Audífonos', guidance: 'No inferir geometría de habitación a partir del audio de audífonos. Usar HRTF solo si el perfil y estado efectivo están confirmados; evitar crosstalk de altavoces.' },
  'nearfield': { label: 'Escritorio / nearfield', guidance: 'Priorizar imagen centrada, reflexiones tempranas bajas y balance moderado; sin medición no inferir RT60 ni dimensiones.' },
  'living-room': { label: 'Sala / living', guidance: 'Tratar modos de graves y reflexiones como hipótesis hasta medir; recomendar colocación/medición antes de EQ fuerte.' },
  'treated-studio': { label: 'Estudio tratado', guidance: 'Priorizar neutralidad, respuesta reproducible y margen dinámico; no añadir sala virtual sin solicitud.' },
  'club': { label: 'Club / pista', guidance: 'Controlar energía acumulada de graves y cobertura; no aumentar SPL; mediciones de presión y alineación deben ser externas y calibradas.' },
  'arena': { label: 'Arena / concierto masivo', guidance: 'Modelar por zonas y arreglos L/R/center/sub/fills únicamente con datos de sistema; no asumir geometría ni retrasos sin medición.' },
  'theatre': { label: 'Teatro / sala elite', guidance: 'Preservar inteligibilidad, cobertura uniforme y dinámica; las reflexiones y RT deben medirse con procedimiento apropiado.' },
  'outdoor': { label: 'Exterior / festival', guidance: 'No asumir reverberación de sala; considerar viento, temperatura, distancias y directividad solo si hay sensores/datos disponibles.' },
  'unknown': { label: 'Sin clasificar', guidance: 'Mantener perfil neutro y pedir calibración voluntaria; no fabricar un modelo de recinto.' },
} as const;
export type VenueId = keyof typeof VENUE_PROFILES;

export const OUTPUT_ROUTES = {
  'built-in': 'Altavoz integrado: capacidad y respuesta desconocidas; EQ conservadora y límite de nivel.',
  'wired-3.5mm': 'Jack 3.5 mm: ruta analógica; el teléfono no puede conocer automáticamente la respuesta de los altavoces/amplificador conectados.',
  'usb-c': 'USB-C: detectar ruta/formato cuando el sistema lo exponga; el DAC externo puede tener límites propios.',
  'bluetooth': 'Bluetooth: códec, perfil y latencia dependen de ambos extremos; no inferir calidad ni respuesta del altavoz solo por la conexión.',
  'usb-audio': 'USB Audio: usar sample rate, canales y latencia reportados por el host; respuesta física requiere medición.',
  'unknown': 'Ruta no confirmada: no atribuir al hardware capacidades no medidas.',
} as const;

export const ACOUSTIC_ENGINEERING_CONTEXT = `
FotoHolo acoustic knowledge pack v${FOTOHOLO_KNOWLEDGE_VERSION}:
- Separate three domains: (1) signal processing, (2) electroacoustic system, (3) room/venue. Do not infer room response from genre, metadata, Wi-Fi, or a visual scene.
- Prefer measurement-first: route/sample rate/channel count/latency/clipping/underruns; then calibrated microphone sweep or impulse response with explicit consent; only then estimate frequency response, RT60, early reflections, and speaker-to-listener geometry.
- A phone microphone is not a calibrated SPL microphone by default. Never claim dB SPL, RT60, dimensions, anatomy, or speaker response unless sensor/calibration and uncertainty are supplied.
- Music metadata is optional context, not audio truth. Accept user-provided/local metadata or authorized provider APIs. Never scrape private streams, bypass DRM, or ingest Spotify audio/content into AI; Spotify policy prohibits AI ingestion/training of its platform/content. Do not treat title/artist as authorization to analyze the stream.
- Genre profiles are gentle priorities, not stereotypes or automatic EQ curves. Preserve artist intent, dynamics and headroom; avoid clipping, uncontrolled bass boost, aggressive compression and hearing-risk increases.
- Changes are bounded, reversible, logged, user-visible and can be disabled. AI can propose visual or bounded DSP changes only when explicitly enabled; require confirmation for large changes and never override safety/volume limits.
- Network discovery cannot reveal room dimensions or human anatomy. Only query local device/network details needed for an explicit feature, with OS permission and consent. Do not scan unrelated devices or identify attendees.
- Body/head measurements must not be inferred from Wi-Fi or bystanders. If spatial personalization is offered, use voluntary, local-only calibration by the user with explicit camera/depth-sensor consent, clear preview, and no biometric identity or persistent body scan.
- Venue profile priorities: headphones (HRTF only if runtime-confirmed), nearfield/studio (neutrality), living room (measure modes/reflections), club/arena (coverage, alignment and measured SPL by qualified workflows), theatre (intelligibility/dynamics), outdoor (no assumed room reverb).
`.trim();
