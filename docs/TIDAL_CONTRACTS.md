# TIDAL dentro de IVANNA — Contratos (v2.5.421)

> Documento de referencia del producto. Verificado el 2026-11-12 contra
> developer.tidal.com, la documentación pública de la API de TIDAL y la
> comunidad tidalapi. **Este documento dice la verdad aunque sea incómoda:
> hay un contrato que falta y no se consigue con código.**

## 1. La verdad contractual (lo que está en regla y lo que no)

| Vía | Estado | Qué da |
|---|---|---|
| **Programa oficial de desarrolladores** (developer.tidal.com, gratuito) | ✅ En regla | Catálogo, búsqueda, metadatos, playlists, letras, mixes. API REST `openapi.tidal.com/v2` + SDKs oficiales. Client_id propio registrado. |
| **Player SDK oficial** | ✅ En regla | Reproducción de **previews** (fragmentos). La documentación oficial dice literal: *"The Player module in the SDK constitutes the only allowed way for third-party applications to incorporate playback of TIDAL content"* — y solo previews. |
| **Reproducción completa vía `playbackinfopostpaywall`** (API no oficial, tidalapi) | ⚠️ Funcional, fuera de términos, revocable | Temas íntegros en FLAC hasta 24-bit/192 kHz. Es lo que usa IVANNA hoy (v2.5.416–421) y lo que hacen High Tide, Kelp, tidalbar y cliamp. |
| **Acuerdo comercial / partnership con TIDAL** | ❌ **El contrato que falta** | Aprobación escrita para reproducir temas íntegros dentro de una app de pago. Los Developer Terms 3.0 exigen aprobación escrita para integrar contenido TIDAL con otro servicio y para apps comerciales. **No se solicita por autoservicio: no hay vía visible en el dashboard para pasar a producción.** |

**Consecuencia honesta:** con el programa público se puede construir un
cliente de catálogo legal, pero **no un reproductor completo vendible**.
Para vender IVANNA ($29) con Tidal completo y en regla hace falta el
acuerdo comercial. Ese es el contrato que falta, y se persigue por vía de
negocio (carta en el §6), no con código.

**Riesgo concreto del client_id actual:** el PKCE `6BDSRdpK9hqEBTgU` es la
credencial first-party de la propia app móvil de TIDAL, no una credencial
emitida a IVANNA. TIDAL puede revocarla o rotarla en cualquier momento,
sin aviso ni recurso. Precedente real en 2026: playlistforge tuvo que
cambiar de este client PKCE al flujo device porque el flujo web quedó
bloqueado por reCAPTCHA. **Mitigación en el código:** si el login o el
refresh fallan, IVANNA lo dice claro ("hay que volver a entrar") y no
inventa nada; el catálogo y la reproducción de archivos propios siguen
funcionando. La migración del catálogo a la API oficial v2 (con client_id
propio) está en el plan (§5).

**Límites que IVANNA respeta siempre (sin excepción):**
- Sin bypass de DRM, jamás: `encryptionType != NONE` (o ContentProtection
  en el MPD) → no se reproduce, se informa al usuario.
- Sin secretos de terceros en el repo: el client_id PKCE es público (viaja
  en claro en toda URL de authorize); el device flow exige `client_secret`
  y ese valor **no** se incrusta.
- Solo streaming: la caché es temporal (500 MB / 12 archivos, LRU), no una
  descarga para guardar. Nada se almacena para siempre.
- Las URLs firmadas de CDN nunca llevan el Bearer token (eso lo filtraría
  a un tercero).

## 2. Contrato de audio (calidad)

- **Bit-perfect:** el FLAC de TIDAL llega al BridgePlayer sin re-codificar
  ni bajar de calidad. La cadena magistral (ASRC → ORBE → HOLOS → Φ →
  Volterra) procesa el 100% del audio. Ninguna otra app tiene esa cadena.
- **Calidad honesta:** la etiqueta que ve el usuario es la calidad
  ENTREGADA por el servidor (`FLAC 24-bit/96 kHz`), no la pedida.
- **HiRes de verdad:** hasta 24-bit/192 kHz (DASH segmentado) cuando el
  tema lo trae; LOSSLESS es FLAC 16-bit/44.1 kHz (BTS).
- **Calidad automática (v2.5.421):** AUTO por defecto — WiFi/red sin
  contador → HI_RES; móvil con contador → LOSSLESS. Override manual:
  Auto → MAX HiRes → LOSSLESS. La preferencia vieja se migra (no se pierde
  la elección de Esse).

## 3. Contrato de reproducción

- **Sin cortes:** el tema se descarga completo antes de sonar (v2.5.418)
  — con la red inestable el extractor marcaba EOS prematuro y la cola
  avanzaba sola. Coste honesto: unos segundos de carga por tema en WiFi.
- **Transición instantánea (v2.5.421):** el siguiente tema de la cola se
  resuelve MIENTRAS suena el actual (prefetch). Si el prefetch falla, el
  tema se resuelve al llegar, como antes.
- **Repetición instantánea (v2.5.421):** caché acotada de los últimos
  temas (500 MB / 12 archivos, LRU). Funciona sin red para lo ya escuchado.
- **Sin avance fantasma:** un STOPPED transitorio del play() interno o un
  error del reproductor no avanza la cola. Solo el fin real de un tema
  avanza. Un tema cifrado se rechaza con mensaje claro, no se salta en
  silencio.
- **Reintento (v2.5.421):** un corte de red de un segundo no mata el tema:
  la descarga reintenta una vez (1 s) antes de fallar.
- **Telemetría (v2.5.421):** cada descarga registra bytes, tiempo,
  calidad entregada y si salió de la caché (`TidalDownloadStats`, visible
  en logcat `IVANNA.TidalStream` / `IVANNA.TidalPlayer`).

## 4. Contrato de privacidad y beneficio (para Esse, su producto y los suyos)

- **Los tokens son del usuario, en su teléfono:** cifrados en
  `TidalTokenStore`, nunca se loguean, nunca salen del dispositivo.
- **El login es del usuario, en la página de TIDAL:** Esse escribe SUS
  credenciales en el WebView de TIDAL; IVANNA solo recibe el `code`.
- **Sin analítica ni venta de datos:** IVANNA no envía a TIDAL nada más
  que las llamadas de API necesarias para buscar y reproducir. No hay
  tracking de escucha, no hay anuncios, no se vende el historial.
- **La biblioteca es del usuario:** sus playlists y favoritos se leen de
  su cuenta y se cachean localmente; nada se sube a ningún servidor de
  IVANNA.
- **El beneficio es para el usuario:** la cadena magistral y la calidad
  HiRes son para que ÉL escuche mejor, no para que TIDAL gane nada.
  IVANNA no cobra por la música de TIDAL: cobra por su procesamiento.

## 5. Contrato de resiliencia

- **Refresh:** el access token se refresca 60 s antes de caducar. Si el
  refresh falla (`invalid_client` / `invalid_grant`), la sesión se
  invalida y se pide re-login — nunca se queda en un estado roto.
- **Backoff:** 429 y 5xx reintentan con backoff (hasta 3 intentos).
  `countryCode` y `sessionId` se inyectan en toda petición (v2.5.417).
- **Degradación honesta:** si TIDAL revoca el client_id, el login deja de
  funcionar y IVANNA lo dice; la app sigue reproduciendo archivos locales
  y la captura (Ruta A) sigue funcionando.
- **Plan de migración (pendiente):** catálogo a la API oficial v2 con
  client_id propio registrado en developer.tidal.com (paso 1: registrar la
  app; paso 2: migrar búsqueda/listas/letras; paso 3: previews con el
  Player SDK como fallback si el endpoint no oficial cae).

## 6. El contrato que falta — solicitud de acuerdo comercial (borrador)

> Para enviar desde la cuenta de Esse a TIDAL (developer.tidal.com /
> partnerships). En inglés, porque es el idioma de negocio de TIDAL.
> No enviar sin que Esse lo revise y lo apruebe.

```
Subject: Commercial partnership request — IVANNA OMEGA SUPREME (Android audio player)

Hello TIDAL team,

My name is Luis Uriel Pimentel Pérez, independent developer of IVANNA
OMEGA SUPREME, a proprietary Android audio player (Android 13+) sold as
a one-time purchase. IVANNA applies a proprietary real-time audio
processing chain (resampling servo, binaural rendering, field fusion)
to the user's own TIDAL subscription streams, at full quality
(up to 24-bit/192 kHz FLAC).

I am writing to request written approval / a commercial agreement to
integrate full-track TIDAL playback inside IVANNA for paying TIDAL
subscribers, under the TIDAL Developer Terms. I already use the public
Developer Program for catalog access and I am ready to comply with
attribution, branding and usage requirements, and to use official
credentials issued to my application.

Product: IVANNA OMEGA SUPREME (Android, com.ivanna.omega)
Model: one-time purchase, no ads, no resale of TIDAL content.
Users: paying TIDAL subscribers only; playback requires their own
active TIDAL subscription.

Please let me know the process, requirements and contact for a
partnership or commercial approval.

Thank you,
Luis Uriel Pimentel Pérez
luisurielpimentelperez814@gmail.com
```

## Fuentes (verificado 2026-11-12)

- developer.tidal.com — portal y documentación oficial de la API/SDK.
- https://developer.tidal.com/documentation/api-sdk/api-sdk-overview
  (Player module = única vía permitida; solo previews).
- https://github.com/orgs/tidal-music/discussions/78 (playback solo vía SDK).
- https://github.com/EbbLabs/python-tidal — API no oficial (tidalapi).
- https://github.com/bjarneo/cliamp/pull/313 (BTS vs DASH, client_id PKCE).
- https://sonovault.now/tidal-api (resumen de Developer Terms 3.0,
  contrastado con la documentación pública el 2026-10-19).
