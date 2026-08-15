import os

f = "./app/src/main/java/com/ivanna/omega/core/CloudSyncManager.kt"
with open(f, 'r') as file:
    content = file.read()

# Add a toggle variable inside the object
if "var isFirebaseOptInEnabled = false" not in content:
    content = content.replace("object CloudSyncManager {", "object CloudSyncManager {\n    var isFirebaseOptInEnabled = false\n")

# Patch ensureInit to respect opt-in
ensure_init_patch = """
    private fun ensureInit(context: Context): Boolean {
        if (!isFirebaseOptInEnabled) {
            Log.i(TAG, "Firebase sync is not explicitly opted-in. Skipping sync.")
            return false
        }
        if (initialized) return true
"""
if "if (initialized) return true" in content and "if (!isFirebaseOptInEnabled)" not in content:
    content = content.replace("    private fun ensureInit(context: Context): Boolean {\n        if (initialized) return true", ensure_init_patch)

with open(f, 'w') as file:
    file.write(content)
print("Patched CloudSyncManager.kt")
