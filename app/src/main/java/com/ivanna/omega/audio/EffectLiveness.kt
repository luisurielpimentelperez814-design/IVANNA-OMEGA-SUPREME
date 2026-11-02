package com.ivanna.omega.audio

/** Criterio puro (testeable en JVM) para decidir si un AudioEffect sigue vivo tras un sondeo. */
internal object EffectLiveness {
    /** android.media.audiofx.AudioEffect.SUCCESS == 0; cualquier otro valor (DEAD_OBJECT, etc.) = muerto. */
    const val SUCCESS = 0
    const val DEAD = -32   // DEAD_OBJECT (-EPIPE) o excepcion al sondear
    fun isAlive(status: Int?): Boolean = status == SUCCESS
}
