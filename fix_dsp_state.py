import re

with open("app/src/main/java/com/ivanna/omega/audio/DspStateUpdater.kt", "r") as f:
    content = f.read()

# Add lastSpatialWidth
if "val lastSpatialWidth" not in content:
    content = content.replace("fun getLastAppliedState(): AudioState? = lastState", "fun getLastAppliedState(): AudioState? = lastState\n    \n    val lastSpatialWidth: Float\n        get() = lastState?.spatialWidth ?: 0f")

# Update OmegaMetrics when spatial width changes
if "OmegaMetrics.updateSharedLevels" not in content:
    insertion = """                runCatching {
                    IvannaNativeLib.nativeSetSpatialWidthDirect(
                        newState.spatialWidth.coerceIn(0f, 2f)
                    )
                }.onFailure { Log.w(TAG, "spatial native: ${it.message}") }
                com.ivanna.omega.audio.OmegaMetrics.updateSharedLevels(spatialWidth = newState.spatialWidth)"""
    
    content = re.sub(
        r"runCatching \{\s*IvannaNativeLib\.nativeSetSpatialWidthDirect\(\s*newState\.spatialWidth\.coerceIn\(0f, 2f\)\s*\)\s*\}\.onFailure \{ Log\.w\(TAG, \"spatial native: \$\{it\.message\}\"\) \}",
        insertion,
        content
    )

with open("app/src/main/java/com/ivanna/omega/audio/DspStateUpdater.kt", "w") as f:
    f.write(content)
