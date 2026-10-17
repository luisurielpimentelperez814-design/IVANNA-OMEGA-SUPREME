// FourDControlSurface.hpp — Superficie de control GLOBAL de la Cadena 4D
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// PROPÓSITO: cierra el cableado punta a punta de la cadena 4D. Los módulos
// (ExcitationConductor, MultibandHapticMapper, CrosstalkCanceller, HeadTrackerFusion,
// ExposureDosimeter, FourDExcitationChain — commit 0ff15271) existían y se probaban en
// host, pero NO estaban alcanzables desde el dispositivo: el target nativo
// libivanna_omega.so no los compilaba y no existía ni JNI ni UI. Esta superficie:
//
//   · posee UNA instancia global de FourDExcitationChain (el SceneTargetBus es
//     singleton POR PROCESO, así que una instancia por proceso es lo correcto);
//   · se alimenta desde los MISMOS puntos que ya usaba el motor IME (ImeBridge), de modo
//     que la ruta de audio real la mueve sin tocar el JNI principal;
//   · expone controles atómicos y estado para el JNI y la UI;
//   · TODO APAGADO POR DEFECTO: con enabled==false no se altera un solo bit (identidad
//     estricta) — misma filosofía del Atlas (con confianza baja, salida == entrada).
//
// RT-safe en feed/apply: sin malloc (búferes fijos reutilizados), sin locks, sin excepciones.
#pragma once

#include "FourDExcitationChain.hpp"
#include <cstdint>

namespace ivanna { namespace fourd {

// ── Ciclo de vida (hilo propietario; idempotente) ────────────────────────────
void ensureInit(float sampleRate, int maxBlock) noexcept;
bool isInitialized() noexcept;
void shutdown() noexcept;

// ── Interruptor maestro ──────────────────────────────────────────────────────
void setEnabled(bool on) noexcept;
bool enabled() noexcept;

// ── Sub-controles (atómicos; invocables desde UI/JNI en cualquier hilo) ───────
void setHapticsEnabled(bool on) noexcept;
void setHapticScale(float s) noexcept;
void setOutputLatencyMs(float ms) noexcept;
void setLeadMs(float ms) noexcept;          // clampado a [5,10] ms por el conductor
void setDepth(float d) noexcept;            // profundidad de modulación [0,1]
void setArcEnabled(bool on) noexcept;       // arco narrativo: subida · hueco · impacto (apagado por defecto)
void setArcDepth(float d) noexcept;         // profundidad del arco [0,1] (0 = identidad exacta)
void setDosimeterMode(int mode) noexcept;   // 0 WHO · 1 NIOSH · 2 OSHA
void setDosimeterCalibration(float dbfsToSpl) noexcept;
void setCrosstalkEnabled(bool on) noexcept; // solo por AUX/altavoces
void setSpeakerGeometry(const ivanna::spatial::CrosstalkCanceller::Geometry& g) noexcept;

// ── Alimentación (hilo de análisis) ──────────────────────────────────────────
void feedPlanar(const float* L, const float* R, int frames) noexcept;
void feedInterleaved(const float* interleaved, int frames, int channels) noexcept;

// ── Etapa de salida (cancelación de diafonía in-place por altavoces) ─────────
// Va ANTES del SafetyLimiter: la cancelación puede subir picos y el limitador es el techo final.
void applyOutputPlanar(float* L, float* R, int frames) noexcept;
void applyOutputInterleaved(float* interleaved, int frames, int channels) noexcept;

// ── Medición de la señal FINAL (post-limitador) para el dosímetro ────────────
// Solo lee. Mientras se llame cada bloque, el dosímetro mide la salida real y deja de usar la
// entrada seca; si se deja de llamar, vuelve solo a medir la entrada.
void meterOutputPlanar(const float* L, const float* R, int frames) noexcept;

// ── Lazo de escena ───────────────────────────────────────────────────────────
// Toma la escena vigente del bus (producida por el StyleBlender/IME) y publica la versión
// ADELANTADA por el conductor. Idempotente si la cadena está apagada.
// Distingue la escena AJENA (base) de la PROPIA ya modulada: se modula siempre sobre la última
// base ajena, nunca sobre su propia salida, para que la apertura no se acumule bloque a bloque.
void publishAnticipatedScene() noexcept;

// ── Estado para telemetría / UI ──────────────────────────────────────────────
ivanna::ime::FourDStatus status() noexcept;
uint32_t publishedScenes() noexcept;

}} // namespace ivanna::fourd
