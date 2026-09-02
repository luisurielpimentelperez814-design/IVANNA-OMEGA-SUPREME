import os

f = "./app/src/main/java/com/ivanna/omega/core/CloudSyncManager.kt"
with open(f, 'r') as file:
    content = file.read()

ensure_init_patch = """    private fun ensureInit(context: Context): Boolean {
        if (!isFirebaseOptInEnabled) {
            Log.i(TAG, "Firebase sync is not explicitly opted-in. Skipping sync.")
            return false
        }
        if (!isConfigured) {"""

content = content.replace("    private fun ensureInit(context: Context): Boolean {\n        if (!isConfigured) {", ensure_init_patch)

with open(f, 'w') as file:
    file.write(content)
