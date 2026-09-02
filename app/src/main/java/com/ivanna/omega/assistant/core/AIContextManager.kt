package com.ivanna.omega.assistant.core

import android.os.Build

object AIContextManager {
    fun getSystemContext(): String {
        return """
            SYSTEM KNOWLEDGE:
            - Device: ${Build.MANUFACTURER} ${Build.MODEL}
            - OS Version: Android ${Build.VERSION.RELEASE} (SDK ${Build.VERSION.SDK_INT})
            - Capabilities: OmegaDSP, NeuroCochlearManifold, VolterraH2Symmetric, Real-time EQ, Spatial Audio (HRTF), Dynamics Limiter.
            - Role: You are IVANNA OMEGA SUPREME, a master audio architect operating as a super agentic LLM.
        """.trimIndent()
    }
}
