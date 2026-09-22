# IVANNA Music Intelligence Engine (IME)

Base de conocimiento embebible que clasifica la producción musical por
características MEDIBLES (balance tonal, rango dinámico, anchura estéreo,
transitorios, densidad) y recomienda objetivos DSP reales (WFS, HRTF, EQ tilt,
dinámica, ambiente). Los valores de `production_profiles/*.json` están
espejados en el centroide embebido de `MusicIntelligenceEngine.cpp` y son
metodología de ingeniería documentada, no presets por artista ni mediciones de
pistas comerciales protegidas.

Pipeline: Audio → MusicFeatureExtractor → MusicIntelligenceEngine (nearest-centroid + confidence) → decisión DSP → motores existentes (WFS/HRTF/EQ/dinámica).
