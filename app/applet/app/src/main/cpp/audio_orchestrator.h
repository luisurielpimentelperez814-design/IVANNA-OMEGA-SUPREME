#pragma once

#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

// Master Orchestration & Acoustic Stability Sanitizer
void ivanna_orchestrate(float* buffer, int samples, int channels, int sampleRateHz);

// Parameter Controls (Lock-free atomic writes)
void ivanna_set_anti_dolby_scores(float speech, float music, float bass);
void ivanna_set_route_profile(float bassBoostDb, float dialogBoostDb, float widenerMult);
void ivanna_set_manifold_enabled(bool enabled);
void ivanna_set_master_gain(float db);
void ivanna_set_eq_gain(float db);
void ivanna_set_stereo_width(float width);

// Real-time Telemetry (Lock-free atomic reads)
float ivanna_get_lufs();
float ivanna_get_peak_dbfs();

// Spatial HRTF Controls
void ivanna_set_hrtf_wet_dry(float wet);
void ivanna_flush_hrtf_history();

#ifdef __cplusplus
}
#endif
