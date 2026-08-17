#!/data/data/com.termux/files/usr/bin/bash
set -e

python3 - <<'PY'
from pathlib import Path

# ---- Fix CloudSyncManager ----
p = Path("app/src/main/java/com/ivanna/omega/core/CloudSyncManager.kt")
s = p.read_text()

s = s.replace(
""".distinctBy { it.timestamp to it.presetName }
                    .sortedBy { it.timestamp }""",
""".distinctBy { profile ->
                        profile.timestamp to profile.presetName
                    }
                    .sortedBy { profile ->
                        profile.timestamp
                    }"""
)

p.write_text(s)


# ---- Fix IVANNAApplication syncDown signature ----
p = Path("app/src/main/java/com/ivanna/omega/core/IVANNAApplication.kt")
s = p.read_text()

s = s.replace(
"""CloudSyncManager.syncDown(this@IVANNAApplication, UserProfileManager(this@IVANNAApplication))""",
"""CloudSyncManager.syncDown(
                      this@IVANNAApplication,
                      UserProfileManager(this@IVANNAApplication)
                  )"""
)

p.write_text(s)


# ---- Add replaceHistory if missing ----
p = Path("app/src/main/java/com/ivanna/omega/ai/UserProfileManager.kt")

if p.exists():
    s = p.read_text()

    if "fun replaceHistory" not in s:
        insert = """
    
    fun replaceHistory(history: List<UserProfile>) {
        saveHistory(history)
    }
"""
        pos = s.rfind("}")
        s = s[:pos] + insert + s[pos:]
        p.write_text(s)

print("CloudSync/App patch complete")
PY

git add .
git commit -m "fix: repair CloudSync history merge and syncDown call"
git push origin main

echo DONE
