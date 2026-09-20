# Auditoria UI — barrido completo (2026-09-20)

Pantallas escaneadas: **43**. Callbacks vacios encontrados: **0**. Archivos cableados: **0**.

Mecanismo: `core/UiActionBridge` (SharedPreferences namespaced por pantalla).
Todo callback interactivo vacio persiste su valor/accion — cero controles de adorno sin restauracion.
Los controles con efecto DSP conservan su ruta JNI/ParameterStore intacta.

| Pantalla | Sliders | Switches | Botones | ParameterStore | JNI | Vacios→Cableados |
|---|---|---|---|---|---|---|
| AbxTestScreen.kt | 2 | 0 | 9 | no | si | 0→0 |
| AdaptiveControlsPrefs.kt | 0 | 0 | 0 | no | no | 0→0 |
| AdaptiveDashboard.kt | 0 | 0 | 0 | no | si | 0→0 |
| AdaptiveEngineCard.kt | 0 | 0 | 0 | si | si | 0→0 |
| AdaptiveEngineLivePanel.kt | 0 | 0 | 0 | no | si | 0→0 |
| AdaptiveEngineScreen.kt | 0 | 0 | 0 | no | si | 0→0 |
| AdaptiveProfilesScreen.kt | 0 | 0 | 0 | si | no | 0→0 |
| AuditoryExperienceScreen.kt | 0 | 0 | 1 | no | no | 0→0 |
| Bark64VisualizerPanel.kt | 0 | 0 | 0 | no | no | 0→0 |
| BenchmarkScreen.kt | 0 | 0 | 2 | no | no | 0→0 |
| BrainScreen.kt | 3 | 1 | 4 | no | si | 0→0 |
| BridgePlayerCard.kt | 1 | 0 | 6 | no | no | 0→0 |
| CmaEsFitnessPanel.kt | 1 | 0 | 0 | no | si | 0→0 |
| CognitiveDashboardActivity.kt | 1 | 0 | 1 | no | no | 0→0 |
| ControlTabScreen.kt | 0 | 0 | 0 | si | si | 0→0 |
| EngineStatusCard.kt | 0 | 0 | 0 | no | no | 0→0 |
| EnginesStatusScreen.kt | 0 | 0 | 0 | no | si | 0→0 |
| FftOscilloscopePanel.kt | 1 | 0 | 0 | no | si | 0→0 |
| HarmonicExciterPanel.kt | 1 | 1 | 0 | no | si | 0→0 |
| HiResAudioScreen.kt | 0 | 0 | 0 | no | no | 0→0 |
| Iso226CalibratorPanel.kt | 2 | 0 | 1 | no | no | 0→0 |
| IvannaAssistantScreen.kt | 0 | 0 | 4 | no | no | 0→0 |
| IvannaControlPanel.kt | 0 | 2 | 6 | no | si | 0→0 |
| IvannaLabScreen.kt | 0 | 1 | 2 | no | no | 0→0 |
| IvannaNavigation.kt | 0 | 0 | 0 | no | si | 0→0 |
| IvannaOmniComponents.kt | 1 | 1 | 0 | no | no | 0→0 |
| IvannaRoute.kt | 0 | 0 | 0 | no | no | 0→0 |
| MagiskStatusPanel.kt | 0 | 0 | 0 | no | si | 0→0 |
| MagistralDashboardScreen.kt | 0 | 0 | 3 | no | no | 0→0 |
| NeonProfilerPanel.kt | 0 | 0 | 0 | no | si | 0→0 |
| NetworkStatusPanel.kt | 0 | 0 | 2 | no | no | 0→0 |
| PerceptualBrainDashboard.kt | 1 | 0 | 1 | no | no | 0→0 |
| Phase7Screen.kt | 0 | 0 | 1 | no | no | 0→0 |
| PinnaMetricsSection.kt | 3 | 0 | 1 | no | si | 0→0 |
| ProfileSelector.kt | 0 | 0 | 0 | no | si | 0→0 |
| SaFCalibrationScreen.kt | 0 | 0 | 6 | no | si | 0→0 |
| SofaAfRirSafPanelScreen.kt | 1 | 1 | 3 | no | si | 0→0 |
| SoundScreen.kt | 1 | 2 | 0 | no | si | 0→0 |
| SpatialAudioPanel.kt | 1 | 1 | 0 | no | si | 0→0 |
| SpatialAudioPrefs.kt | 0 | 0 | 0 | no | no | 0→0 |
| SpatialControlPanel.kt | 1 | 1 | 1 | no | si | 0→0 |
| SystemScreen.kt | 0 | 0 | 4 | no | si | 0→0 |
| TinyMlClassifierPanel.kt | 1 | 0 | 1 | no | si | 0→0 |
