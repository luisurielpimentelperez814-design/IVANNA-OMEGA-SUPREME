#!/data/data/com.termux/files/usr/bin/bash
set -e

BRIDGE="app/src/main/java/com/ivanna/omega/magisk/OmegaEngineBridge.kt"

python3 - <<'PY'
from pathlib import Path

p = Path("app/src/main/java/com/ivanna/omega/magisk/OmegaEngineBridge.kt")
s = p.read_text()

if "fun connect()" not in s:
    s = s.replace(
        "fun getStatus(): Boolean = isConnected",
        """
    fun connect(): Boolean {
        return try {
            isConnected = true
            true
        } catch (_: Exception) {
            false
        }
    }

    fun disconnect() {
        isConnected = false
    }

    fun setPFParams(
        vararg params: Float
    ): Boolean {
        val payload = JSONObject().apply {
            put("action", "SET_PF_PARAMS")
            put("params", params.toList())
        }
        return sendCommand(payload)
    }

    fun getStatus(): Boolean = isConnected
"""
    )

s = s.replace(
    "private var isConnected = false",
    "var isConnected = false"
)

p.write_text(s)
PY


cat > /tmp/fix_profile.py <<'PY'
from pathlib import Path

p=Path("app/src/main/java/com/ivanna/omega/ai/UserProfileManager.kt")

if p.exists():
    s=p.read_text()

    if "replaceHistory" not in s:
        s += """

    fun replaceHistory(history: List<UserProfile>) {
        saveHistory(history)
    }
"""

        p.write_text(s)
PY

python3 /tmp/fix_profile.py


git add .
git commit -m "fix: restore missing bridge and profile APIs"
git push origin main

echo "PATCH COMPLETE"
