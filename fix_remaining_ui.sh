#!/bin/bash
set -e

python3 - <<'PY'
from pathlib import Path

# Agregar disconnect al bridge
p = Path("app/src/main/java/com/ivanna/omega/magisk/OmegaEngineBridge.kt")
s = p.read_text()

if "fun disconnect()" not in s:
    s = s.replace(
        "fun getStatus(): Boolean = isConnected",
        """fun disconnect() {
        isConnected = false
    }

    fun getStatus(): Boolean = isConnected"""
    )

p.write_text(s)


# Corregir MainActivity
p = Path("app/src/main/java/com/ivanna/omega/ui/MainActivity.kt")
s = p.read_text()

old = "val state by vm.uiState.collectAsState()"

new = """val state by vm.uiState.collectAsState()
    val dspDecision by vm.dspDecision.collectAsState()
    val userProfile by vm.userProfile.collectAsState()"""

s = s.replace(old,new)

s = s.replace(
"state.dspDecision.executionLatencyMs",
"dspDecision.executionLatencyMs"
)

s = s.replace(
"state.dspDecision",
"dspDecision"
)

s = s.replace(
"state.userProfile.aggressiveness",
"userProfile.aggressiveness"
)

p.write_text(s)

PY

echo "remaining UI fixes applied"
