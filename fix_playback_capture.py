import re

with open("app/src/main/java/com/ivanna/omega/audio/PlaybackCaptureService.kt", "r") as f:
    content = f.read()

# Add blockCounter
content = re.sub(
    r"val mono\s*=\s*FloatArray\(BLOCK_FRAMES\)",
    "val mono   = FloatArray(BLOCK_FRAMES)\n                var blockCounter = 0",
    content
)

# Add metrics update
insertion = """                    runCatching { IvannaVisualizerBark64Bridge.processBlock(mono, frames) }
                    
                    // FIX 3: Publish real capture levels for Route A
                    runCatching {
                        var sumSq = 0f
                        var peak = 0f
                        var clips = 0
                        for (i in 0 until read) {
                            val s = buffer[i]
                            sumSq += s * s
                            val absS = kotlin.math.abs(s)
                            if (absS > peak) peak = absS
                            if (absS >= 0.999f) clips++
                        }
                        val rms = kotlin.math.sqrt(sumSq / read.coerceAtLeast(1).toFloat())
                        
                        blockCounter++
                        if (blockCounter % 4 == 0) {
                            com.ivanna.omega.audio.OmegaMetrics.updateSharedLevels(
                                rms = rms,
                                peak = peak,
                                clips = clips,
                                dspActive = true
                            )
                        }
                    }"""

content = content.replace("                    runCatching { IvannaVisualizerBark64Bridge.processBlock(mono, frames) }", insertion)

with open("app/src/main/java/com/ivanna/omega/audio/PlaybackCaptureService.kt", "w") as f:
    f.write(content)
