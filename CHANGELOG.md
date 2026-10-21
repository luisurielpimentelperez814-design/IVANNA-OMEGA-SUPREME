# CHANGELOG — v2.5.108 (399) — bounded HAL output-rate servo

- Added HardwareClockRateServo: converts valid hardware clock drift estimates into a variable-rate ASRC command, requires three valid samples, clamps to ±500 ppm, limits slew to 25 ppm/update, and returns unity ratio when unlocked or invalid.
- Added host tests for lock gating, tracking direction, slew limits and fail-closed behavior.
- Updated HAL integration instructions: the servo belongs beside the HAL output stream and must drive the existing ASRC, not an AudioFlinger insert effect.
- Hardware boundary status: repository still lacks the target vendor HAL source and actual device ASRC call-site. This commit implements the bounded control primitive and tests; it does not claim the phone DAC is already under active control. Target HAL integration and device measurements remain mandatory.

See repository history for prior release notes.
