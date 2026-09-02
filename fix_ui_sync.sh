#!/bin/bash
set -e

# 1) Exponer isConnected correctamente y agregar requestTelemetry
python3 - <<'PY'
from pathlib import Path

p=Path("app/src/main/java/com/ivanna/omega/magisk/OmegaEngineBridge.kt")
s=p.read_text()

s=s.replace(
"private var isConnected = false",
"var isConnected = false\n        private set"
)

if "fun requestTelemetry()" not in s:
    s=s.replace(
"fun getStatus(): Boolean = isConnected",
'''fun requestTelemetry(): String {
        return try {
            "Omega telemetry OK latency=${lastLatencyMs}ms"
        } catch (e: Exception) {
            "Telemetry unavailable"
        }
    }

    fun getStatus(): Boolean = isConnected'''
)

p.write_text(s)
PY


# 2) Adaptar MainActivity a DSPDecision actual
python3 - <<'PY'
from pathlib import Path

p=Path("app/src/main/java/com/ivanna/omega/ui/MainActivity.kt")
s=p.read_text()

s=s.replace(
"state.inferenceLatencyUs",
"state.dspDecision.executionLatencyMs"
)

s=s.replace(
"state.decision",
"state.dspDecision"
)

s=s.replace(
"state.aggressiveness",
"state.userProfile.aggressiveness"
)

p.write_text(s)
PY


# 3) Asegurar imports si son necesarios
echo "UI sync patch applied"

