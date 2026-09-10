# ARCHIVO LEGACY (sin build, sin consumidores)
Auditoría 2026-09-10 (verificada, no asumida):
- ivanna_jni_unified.cpp: exporta SOLO Java_com_ivanna_omega_unified_IvannaUnifiedNative_initEngine (stub JNI_TRUE).
- ivanna_unified_engine.{hpp,cpp}: incluidos SOLO entre sí — par sin consumidores.
- IvannaTinyML.{cpp,hpp}: incluidos SOLO entre sí — sin consumidores.
- Kotlin NO declara ninguna nativa de estos módulos (grep verificado).
Reversión: git mv de vuelta a app/src/main/cpp/ si un flanco los reactiva.
