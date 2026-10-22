# Kit de promoción — IVANNA OMEGA SUPREME

Todo el texto usa solo lo que el repositorio puede demostrar (código, CI, datasets incluidos). Sin cifras inventadas. Imágenes: `ivanna_promo_card_es.png` / `_en.png` (1200×675) y `../release_media/ivanna_social_preview.png`.

## Antes de publicar (importante)
1. **Propiedad:** el producto es propietario y comercial. El acceso público al repositorio no concede derechos de reutilización del código propio. Revisa por separado las licencias de dependencias y datasets antes de distribuir.
2. **APK de prueba:** enlaza la última *Release* de GitHub para que cualquiera pueda instalarlo.
3. **Un video de 30–60 s** grabando la pantalla con el teléfono (calibración HRTF + cambio de perfil). Es lo que más alcance da y no se puede generar sin el dispositivo.

## Mensaje en una frase
- ES: «Un motor de audio para Android que da sonido 3D con HRTF real, sin chasquidos al cambiar volumen o pista, con root (Magisk) o sin root (DAC USB). Todo el código está en GitHub.»
- EN: «An Android audio engine with real-HRTF 3D sound and no clicks when volume or track changes — works rooted (Magisk) or non-root (USB DAC). Commercial product; contact the rights holder for licensing.»

## Publicación por plataforma
**X / Mastodon / Bluesky (ES)**
> Hice un motor de audio para Android desde cero en C++26: sonido 3D con HRTF real, sin chasquidos al cambiar volumen, con root o sin root (DAC USB). Producto propietario con pruebas automáticas en cada cambio. 🎧 github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME #Android #Audio #DSP #Magisk

**X (EN)**
> I built an Android audio engine from scratch in C++26: real-HRTF 3D sound, no clicks on volume/track changes, rooted (Magisk) or non-root (USB DAC). Automated tests on every commit. 🎧 github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME #Android #DSP

**Reddit** — r/androidapps, r/Magisk, r/audiophile, r/cpp, r/androiddev (lee las reglas de cada uno sobre autopromoción; publica en uno por día)
> **Título:** Show: motor de audio para Android (HRTF 3D, sin root o con Magisk) — busco gente que lo pruebe
> Llevo meses construyéndolo. Qué hace: audio espacial 3D usando datasets HRTF/SOFA, procesado sin chasquidos al cambiar volumen o pista, y dos rutas: con root (módulo Magisk) y sin root (salida USB DAC). Está en C++26 con NEON ARM64 y la CI corre pruebas en cada cambio. Es un proyecto en desarrollo: busco testers y críticas honestas. Repo: …

**Hacker News**
> **Show HN: Android audio DSP engine in C++26 (HRTF spatial audio, Magisk + non-root)**
> Primer comentario: explica qué problema resuelves (chasquidos al cambiar volumen/pista, desfase en streaming), qué está hecho y qué falta. Sé específico y humilde; HN castiga el exceso de adjetivos.

**XDA Forums (Android Apps / Magisk Modules)** — hilo con: qué es, instalación (módulo + APK), capturas, cómo reportar bugs, changelog.

**Product Hunt** — tagline: «Audio 3D profesional para Android». Primer comentario del maker + 3 capturas + el video.

**dev.to / Medium / Hashnode** — artículo técnico: «Cómo evitamos chasquidos al cambiar parámetros en un motor de audio en tiempo real» (seqlock, rampas, sin malloc en el hilo de audio). Es contenido verdadero del repo y atrae desarrolladores.

**YouTube / TikTok / Shorts** — 30–60 s: antes/después con audífonos, calibración HRTF, cambio de perfil.

**GitHub** — ya tiene topics (android, dsp, hrtf, magisk-module, spatial-audio…). No anuncies el código propio como reutilizable. Antes de distribuir, revisa los avisos de terceros, las condiciones comerciales y los paquetes publicados. Listas «awesome» donde enviar PR: awesome-android, awesome-audio-dsp, awesome-magisk.

## Reglas para no ser marcado como spam
- Una cuenta tuya real por plataforma, publicando tú (o con tu aprobación).
- Una plataforma por día, adaptando el texto; nunca el mismo copy pegado en todos lados.
- Responde comentarios las primeras 2 horas: eso decide el alcance.
- Di siempre que es un proyecto en desarrollo y qué falta.
