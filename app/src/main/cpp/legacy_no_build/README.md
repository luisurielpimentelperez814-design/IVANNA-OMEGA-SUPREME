# ARCHIVO LEGACY (sin build, sin consumidores activos)
Auditoría 2026-09-10 (verificada, no asumida):
- ivanna_jni_unified.cpp: exporta SOLO Java_com_ivanna_omega_unified_IvannaUnifiedNative_initEngine (stub JNI_TRUE); sin clase Kotlin (grep verificado).
- ivanna_unified_engine.{hpp,cpp}: incluidos SOLO entre sí — par sin consumidores.

ACTUALIZACIÓN 2026-09-10 (corrección de auditoría):
- IvannaTinyML.{cpp,hpp} fueron RESTAURADOS a app/src/main/cpp/ — la primera
  auditoría omitió src/data/cppFiles.ts del flanco Control Dashboard web, que
  los importa con `?raw` (Vite). El archivo habría roto el build del dashboard:
  NO son huérfanos, tienen consumidor (dashboard web).

Reversión de los archivados: git mv de vuelta a app/src/main/cpp/ si un flanco los reactiva.
