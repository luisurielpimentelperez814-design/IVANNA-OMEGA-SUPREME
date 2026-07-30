package com.ivanna.omega.audio.objects

/**
 * Escena de audio inmersiva que contiene un conjunto de objetos dinámicos 3D
 * y una cama de canales (Channel Bed 7.1.4 / 5.1 / Estéreo).
 */
data class AudioScene(
    val sceneId: Long = System.currentTimeMillis(),
    val objects: MutableList<AudioObject> = mutableListOf(),
    val bedChannels: FloatArray = FloatArray(12), // 7.1.4 Channel Bed Buffer
    var sampleRate: Int = 48000,
    var activeObjectCount: Int = 0
) {
    fun addObject(obj: AudioObject) {
        objects.removeAll { it.id == obj.id }
        objects.add(obj)
        activeObjectCount = objects.size
    }

    fun clear() {
        objects.clear()
        bedChannels.fill(0.0f)
        activeObjectCount = 0
    }

    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as AudioScene
        return sceneId == other.sceneId && activeObjectCount == other.activeObjectCount
    }

    override fun hashCode(): Int {
        return sceneId.hashCode()
    }
}
