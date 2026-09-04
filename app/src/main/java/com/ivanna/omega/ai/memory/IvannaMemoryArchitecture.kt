package com.ivanna.omega.ai.memory

import android.content.Context
import android.util.Log
import androidx.security.crypto.EncryptedFile
import androidx.security.crypto.MasterKey
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import java.io.ByteArrayOutputStream
import java.io.File
import java.nio.charset.StandardCharsets
import java.util.UUID
import java.util.concurrent.ConcurrentLinkedQueue

/**
 * IvannaMemoryArchitecture — Sistema de memoria cognitiva de 4 capas.
 * Working → Episodic → Semantic → System
 * Persistencia: EncryptedFile (AES-256-GCM)
 */
class IvannaMemoryArchitecture(context: Context) {
    companion object {
        private const val TAG = "IvannaMemoryArch"
        private const val EPISODIC_FILE = "ivanna_episodic_memory.json"
        private const val SEMANTIC_FILE = "ivanna_semantic_memory.json"
        private const val MAX_EPISODIC_RECORDS = 500
        private const val MAX_WORKING_TURNS = 32
    }

    private val appContext = context.applicationContext
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val json = Json { prettyPrint = false; ignoreUnknownKeys = true; encodeDefaults = true }

    private val masterKey by lazy {
        MasterKey.Builder(appContext)
            .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
            .build()
    }

    val workingMemory = WorkingMemory()
    val episodicMemory: EpisodicMemory = EpisodicMemory { saveEpisodicToDisk() }
    val semanticMemory: SemanticMemory = SemanticMemory { saveSemanticToDisk() }
    val systemMemory = SystemMemory()
    val retrievalEngine = MemoryRetrievalEngine(workingMemory, episodicMemory, semanticMemory, systemMemory)

    private val _isLoaded = MutableStateFlow(false)
    val isLoaded: StateFlow<Boolean> = _isLoaded.asStateFlow()

    init {
        scope.launch {
            loadFromDisk()
            _isLoaded.value = true
            Log.i(TAG, "Memory architecture loaded")
        }
    }

    fun recordInteraction(role: String, text: String) = workingMemory.add(role, text)

    suspend fun persistSession(sessionId: String, userQuery: String, ivannaResponse: String, actions: List<String>) {
        episodicMemory.record(sessionId, userQuery, ivannaResponse, actions)
        saveEpisodicToDisk()
    }

    suspend fun learnFact(key: String, value: String, category: SemanticRecord.SemanticCategory, confidence: Float = 1.0f) {
        semanticMemory.learn(key, value, category, confidence)
        saveSemanticToDisk()
    }

    suspend fun buildContextForGemini(query: String): String = retrievalEngine.buildRichContext(query)
    suspend fun getUserPreference(key: String): String? = semanticMemory.get(key)

    suspend fun pruneOldMemories(olderThanDays: Int = 90) {
        episodicMemory.pruneOld(olderThanDays)
        saveEpisodicToDisk()
    }

    fun updateSystemSnapshot(snapshot: SystemMemory.SystemSnapshot) = systemMemory.update(snapshot)

    fun shutdown() {
        scope.launch { saveEpisodicToDisk(); saveSemanticToDisk() }
        scope.cancel()
    }

    private suspend fun loadFromDisk() = withContext(Dispatchers.IO) {
        runCatching {
            readEncryptedFile(EPISODIC_FILE)?.let {
                episodicMemory.loadRecords(json.decodeFromString(it))
            }
            readEncryptedFile(SEMANTIC_FILE)?.let {
                semanticMemory.loadRecords(json.decodeFromString(it))
            }
        }.onFailure { Log.e(TAG, "Load error: ${it.message}") }
    }

    private suspend fun saveEpisodicToDisk() = withContext(Dispatchers.IO) {
        // FIX (CI rojo): tipos explícitos — la inferencia sobre la lambda
        // onChange + el genérico de encodeToString producía "recursive problem".
        val records: List<EpisodicRecord> = episodicMemory.getAllRecords()
        runCatching { writeEncryptedFile(EPISODIC_FILE, json.encodeToString<List<EpisodicRecord>>(records)) }
            .onFailure { Log.e(TAG, "Save episodic error: ${it.message}") }
    }

    private suspend fun saveSemanticToDisk() = withContext(Dispatchers.IO) {
        val records: List<SemanticRecord> = semanticMemory.getAllRecords()
        runCatching { writeEncryptedFile(SEMANTIC_FILE, json.encodeToString<List<SemanticRecord>>(records)) }
            .onFailure { Log.e(TAG, "Save semantic error: ${it.message}") }
    }

    private fun readEncryptedFile(filename: String): String? {
        val file = File(appContext.filesDir, filename)
        if (!file.exists()) return null
        return runCatching {
            val ef = EncryptedFile.Builder(appContext, file, masterKey, EncryptedFile.FileEncryptionScheme.AES256_GCM_HKDF_4KB).build()
            ef.openFileInput().use { input ->
                ByteArrayOutputStream().use { output ->
                    input.copyTo(output)
                    output.toString(StandardCharsets.UTF_8.name())
                }
            }
        }.getOrNull()
    }

    private fun writeEncryptedFile(filename: String, data: String) {
        val file = File(appContext.filesDir, filename)
        if (file.exists()) file.delete()
        val ef = EncryptedFile.Builder(appContext, file, masterKey, EncryptedFile.FileEncryptionScheme.AES256_GCM_HKDF_4KB).build()
        ef.openFileOutput().use { it.write(data.toByteArray(StandardCharsets.UTF_8)) }
    }

    class WorkingMemory {
        private val buffer = ConcurrentLinkedQueue<Interaction>()
        private val maxSize = MAX_WORKING_TURNS
        data class Interaction(val role: String, val text: String, val timestamp: Long = System.currentTimeMillis())
        fun add(role: String, text: String) {
            buffer.offer(Interaction(role, text))
            while (buffer.size > maxSize) buffer.poll()
        }
        fun getRecent(n: Int): List<Interaction> = buffer.toList().takeLast(n)
        fun getAll(): List<Interaction> = buffer.toList()
        fun clear() = buffer.clear()
    }

    @Serializable
    data class EpisodicRecord(
        val id: String = UUID.randomUUID().toString(),
        val sessionId: String,
        val timestamp: Long = System.currentTimeMillis(),
        val userQuery: String,
        val ivannaResponse: String,
        val actionsTaken: List<String> = emptyList(),
        val summary: String = "",
        val sentiment: Float = 0f
    )

    class EpisodicMemory(private val onChange: suspend () -> Unit) {
        private val records = mutableListOf<EpisodicRecord>()
        private val lock = Object()
        fun loadRecords(new: List<EpisodicRecord>) { synchronized(lock) { records.clear(); records.addAll(new) } }
        fun getAllRecords(): List<EpisodicRecord> = synchronized(lock) { records.toList() }

        suspend fun record(sessionId: String, userQuery: String, ivannaResponse: String, actions: List<String>) {
            val summary = "Q: ${userQuery.take(80)} | A: ${ivannaResponse.take(80)} | Actions: ${if (actions.isEmpty()) "none" else actions.joinToString(", ")}"
            val sentiment = estimateSentiment(userQuery)
            synchronized(lock) {
                records.add(EpisodicRecord(sessionId = sessionId, userQuery = userQuery, ivannaResponse = ivannaResponse, actionsTaken = actions, summary = summary, sentiment = sentiment))
                if (records.size > MAX_EPISODIC_RECORDS) records.removeAt(0)
            }
            onChange()
        }

        fun retrieveRelevant(query: String, topK: Int): List<EpisodicRecord> {
            val q = query.lowercase()
            return synchronized(lock) {
                records.map { it to score(it, q) }.sortedByDescending { it.second }.take(topK).map { it.first }
            }
        }

        suspend fun pruneOld(cutoffDays: Int) {
            val cutoff = System.currentTimeMillis() - cutoffDays * 86400_000L
            synchronized(lock) { records.removeAll { it.timestamp < cutoff } }
            onChange()
        }

        private fun score(r: EpisodicRecord, q: String): Float {
            var s = 0f
            if (r.userQuery.lowercase().contains(q)) s += 2f
            if (r.summary.lowercase().contains(q)) s += 1.5f
            if (r.actionsTaken.any { it.lowercase().contains(q) }) s += 1f
            val ageHours = (System.currentTimeMillis() - r.timestamp) / 3600_000f
            s += (1f / (1f + ageHours / 24f))
            return s
        }

        private fun estimateSentiment(text: String): Float {
            val pos = listOf("gracias", "bien", "excelente", "me gusta", "perfecto", "genial", "increíble")
            val neg = listOf("mal", "no funciona", "error", "problema", "frustrado", "molesto", "odio")
            val t = text.lowercase()
            val p = pos.count { t.contains(it) }
            val n = neg.count { t.contains(it) }
            return when { p > n -> (0.5f + (p - n) * 0.1f).coerceIn(-1f, 1f); n > p -> (-0.5f - (n - p) * 0.1f).coerceIn(-1f, 1f); else -> 0f }
        }
    }

    @Serializable
    data class SemanticRecord(
        val id: String = UUID.randomUUID().toString(),
        val key: String,
        val value: String,
        val category: SemanticCategory,
        val confidence: Float = 1.0f,
        val lastUpdated: Long = System.currentTimeMillis()
    ) {
        @Serializable
        enum class SemanticCategory { USER_PREFERENCE, AUDIO_PREFERENCE, LEARNED_FACT, DEVICE_STATE, PERSONALITY_TRAIT }
    }

    class SemanticMemory(private val onChange: suspend () -> Unit) {
        private val records = mutableListOf<SemanticRecord>()
        private val lock = Object()
        fun loadRecords(new: List<SemanticRecord>) { synchronized(lock) { records.clear(); records.addAll(new) } }
        fun getAllRecords(): List<SemanticRecord> = synchronized(lock) { records.toList() }

        suspend fun learn(key: String, value: String, category: SemanticRecord.SemanticCategory, confidence: Float) {
            synchronized(lock) {
                val idx = records.indexOfFirst { it.key == key }
                val rec = SemanticRecord(key = key, value = value, category = category, confidence = confidence)
                if (idx >= 0) records[idx] = rec else records.add(rec)
            }
            onChange()
        }

        fun retrieveRelevant(query: String, topK: Int): List<SemanticRecord> {
            val q = query.lowercase()
            return synchronized(lock) { records.map { it to score(it, q) }.sortedByDescending { it.second }.take(topK).map { it.first } }
        }

        fun get(key: String): String? = synchronized(lock) { records.find { it.key == key }?.value }

        private fun score(r: SemanticRecord, q: String): Float {
            var s = 0f
            if (r.key.lowercase().contains(q)) s += 3f
            if (r.value.lowercase().contains(q)) s += 2f
            s *= r.confidence
            val ageDays = (System.currentTimeMillis() - r.lastUpdated) / 86400_000f
            s *= (1f / (1f + ageDays / 30f))
            return s
        }
    }

    class SystemMemory {
        private val _snapshot = MutableStateFlow(SystemSnapshot())
        val snapshotFlow: StateFlow<SystemSnapshot> = _snapshot.asStateFlow()

        @Serializable
        data class SystemSnapshot(
            val audioRoute: String = "unknown",
            val sampleRate: Int = 48000,
            val dspChainActive: List<String> = emptyList(),
            val eqProfile: String = "flat",
            val hrtfActive: String = "none",
            val spatialMode: String = "off",
            val presetName: String = "default",
            val cpuLoad: Float = 0f,
            val memoryPressure: String = "normal",
            val thermalTier: String = "nominal",
            val batteryLevel: Int = 100,
            val isCharging: Boolean = false,
            val androidVersion: String = "unknown",
            val deviceModel: String = "unknown",
            val clipEventsLastMinute: Int = 0,
            val daemonConnected: Boolean = false,
            val magiskActive: Boolean = false,
            val timestamp: Long = System.currentTimeMillis()
        )

        fun update(snapshot: SystemSnapshot) { _snapshot.value = snapshot }

        fun snapshot(): String {
            val s = _snapshot.value
            return buildString {
                appendLine("Ruta: ${s.audioRoute} @ ${s.sampleRate}Hz")
                appendLine("DSP: ${s.dspChainActive.joinToString(" → ")}")
                appendLine("EQ: ${s.eqProfile} | HRTF: ${s.hrtfActive} | Spatial: ${s.spatialMode}")
                appendLine("Preset: ${s.presetName}")
                appendLine("CPU: ${(s.cpuLoad * 100).toInt()}% | RAM: ${s.memoryPressure} | Térmico: ${s.thermalTier}")
                appendLine("Batería: ${s.batteryLevel}%${if (s.isCharging) "⚡" else ""}")
                appendLine("Android ${s.androidVersion} | ${s.deviceModel}")
                appendLine("Daemon: ${if (s.daemonConnected) "✓" else "✗"} | Magisk: ${if (s.magiskActive) "✓" else "✗"}")
                appendLine("Clips/min: ${s.clipEventsLastMinute}")
            }
        }
    }

    class MemoryRetrievalEngine(
        private val workingMemory: WorkingMemory,
        private val episodicMemory: EpisodicMemory,
        private val semanticMemory: SemanticMemory,
        private val systemMemory: SystemMemory
    ) {
        suspend fun buildRichContext(query: String, maxTokensEstimate: Int = 4000): String {
            val builder = StringBuilder()
            val sys = systemMemory.snapshot()
            if (sys.isNotBlank()) { builder.appendLine("[SISTEMA]"); builder.appendLine(sys); builder.appendLine() }

            val semantic = semanticMemory.retrieveRelevant(query, topK = 8)
            if (semantic.isNotEmpty()) { builder.appendLine("[USUARIO]"); semantic.forEach { builder.appendLine("- ${it.key}: ${it.value}") }; builder.appendLine() }

            if (impliesHistoricalContext(query)) {
                val episodes = episodicMemory.retrieveRelevant(query, topK = 3)
                if (episodes.isNotEmpty()) { builder.appendLine("[HISTORIAL]"); episodes.forEach { builder.appendLine("- ${it.summary}") }; builder.appendLine() }
            }

            val working = workingMemory.getRecent(6)
            if (working.isNotEmpty()) { builder.appendLine("[CONVERSACIÓN]"); working.forEach { builder.appendLine("${it.role}: ${it.text}") } }

            return builder.toString().trim()
        }

        private fun impliesHistoricalContext(query: String): Boolean {
            val markers = listOf("recuerdas", "antes", "la última vez", "hace rato", "ayer", "la semana pasada", "cuando dije", "como la otra vez", "remember", "last time", "before", "yesterday", "previously", "¿qué hiciste?", "¿qué dije?", "what did you do", "what did i say")
            val q = query.lowercase()
            return markers.any { q.contains(it) }
        }
    }
}
