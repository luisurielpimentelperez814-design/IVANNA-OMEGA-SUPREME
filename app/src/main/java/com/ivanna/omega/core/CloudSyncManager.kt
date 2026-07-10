package com.ivanna.omega.core

import android.content.Context
import android.util.Log
import com.google.firebase.FirebaseApp
import com.google.firebase.FirebaseOptions
import com.google.firebase.auth.FirebaseAuth
import com.google.firebase.firestore.FirebaseFirestore
import com.google.firebase.firestore.ktx.firestoreSettings
import com.google.firebase.firestore.ktx.persistentCacheSettings
import kotlinx.coroutines.tasks.await

/**
 * CloudSyncManager — sincronización de UserProfileManager vía Firebase Firestore.
 *
 * SETUP REQUERIDO (pendiente, Luis tiene que hacerlo — nadie más puede crear
 * su proyecto de Firebase):
 *   1. https://console.firebase.google.com → Crear proyecto (gratis, plan Spark).
 *   2. Dentro del proyecto: Compilación → Firestore Database → Crear base de
 *      datos (modo producción, elegir región cercana, ej. us-central).
 *   3. Reglas de Firestore (Firestore → Reglas) — mínimo para que cada
 *      usuario anónimo solo lea/escriba su propio documento:
 *      ```
 *      rules_version = '2';
 *      service cloud.firestore {
 *        match /databases/{database}/documents {
 *          match /users/{uid} {
 *            allow read, write: if request.auth != null && request.auth.uid == uid;
 *          }
 *        }
 *      }
 *      ```
 *   4. Compilación → Authentication → Sign-in method → habilitar "Anónimo".
 *      (Se usa auth anónima porque la app no tiene pantalla de login — cada
 *      instalación obtiene un uid estable mientras no se borre el almacenamiento
 *      de la app; si más adelante quieres login real para sincronizar entre
 *      dispositivos, hay que agregar un proveedor — Google Sign-In es el más
 *      simple — y vincular la cuenta anónima con linkWithCredential().)
 *   5. Configuración del proyecto (engrane) → Tus apps → Agregar app → Android
 *      → paquete "com.ivanna.omega". Ahí Firebase te da 3 valores (NO hace
 *      falta descargar google-services.json ni aplicar el plugin de Gradle):
 *        - Project ID
 *        - App ID  (formato "1:XXXXXXXXXXXX:android:XXXXXXXXXXXXXXXX")
 *        - Web API Key (la de "General" del proyecto, no la de una app específica)
 *   6. Reemplazar los 3 placeholders TODO_FIREBASE_* de abajo con esos valores.
 *      Mientras sigan siendo el placeholder, isConfigured queda en false y
 *      todas las funciones de esta clase son no-op seguro (no crashea nada).
 *
 * Por qué sin el plugin google-services: es una forma soportada oficialmente
 * por Firebase (FirebaseOptions.Builder + FirebaseApp.initializeApp) y evita
 * que el build se rompa si el archivo de config no está presente — justo el
 * tipo de fallo que se estuvo arreglando en esta misma sesión.
 */
object CloudSyncManager {
    private const val TAG = "CloudSyncManager"

    // ── Rellenar con los valores reales de tu proyecto Firebase (ver setup arriba) ──
    private const val TODO_FIREBASE_PROJECT_ID = "TODO_FIREBASE_PROJECT_ID"
    private const val TODO_FIREBASE_APP_ID = "TODO_FIREBASE_APP_ID"
    private const val TODO_FIREBASE_API_KEY = "TODO_FIREBASE_API_KEY"

    val isConfigured: Boolean
        get() = TODO_FIREBASE_PROJECT_ID != "TODO_FIREBASE_PROJECT_ID" &&
                TODO_FIREBASE_APP_ID != "TODO_FIREBASE_APP_ID" &&
                TODO_FIREBASE_API_KEY != "TODO_FIREBASE_API_KEY"

    @Volatile private var initialized = false
    private var firestore: FirebaseFirestore? = null
    private var auth: FirebaseAuth? = null

    /**
     * Inicializa Firebase manualmente (sin google-services.json) y arranca
     * sesión anónima si no existe. Seguro de llamar múltiples veces. No hace
     * nada si isConfigured es false (placeholders sin rellenar).
     */
    @Synchronized
    private fun ensureInit(context: Context): Boolean {
        if (!isConfigured) {
            Log.w(TAG, "CloudSyncManager no configurado — ver instrucciones de setup en CloudSyncManager.kt. Sync desactivado.")
            return false
        }
        if (initialized) return true
        try {
            val options = FirebaseOptions.Builder()
                .setProjectId(TODO_FIREBASE_PROJECT_ID)
                .setApplicationId(TODO_FIREBASE_APP_ID)
                .setApiKey(TODO_FIREBASE_API_KEY)
                .build()
            if (FirebaseApp.getApps(context).isEmpty()) {
                FirebaseApp.initializeApp(context, options)
            }
            firestore = FirebaseFirestore.getInstance().apply {
                // Cache local offline-first: si no hay red, los reads sirven
                // el ultimo valor conocido y los writes se encolan.
                firestoreSettings = firestoreSettings {
                    setLocalCacheSettings(persistentCacheSettings {})
                }
            }
            auth = FirebaseAuth.getInstance()
            initialized = true
            Log.i(TAG, "Firebase inicializado (proyecto=$TODO_FIREBASE_PROJECT_ID)")
            return true
        } catch (t: Throwable) {
            Log.e(TAG, "ensureInit falló — sync desactivado para esta sesión", t)
            return false
        }
    }

    private suspend fun ensureSignedIn(): String? {
        val a = auth ?: return null
        a.currentUser?.let { return it.uid }
        return try {
            val result = a.signInAnonymously().await()
            result.user?.uid
        } catch (t: Throwable) {
            Log.e(TAG, "signInAnonymously falló", t)
            null
        }
    }

    /**
     * Sube el estado actual de UserProfileManager a Firestore. Best-effort:
     * cualquier fallo (sin red, sin configurar, etc.) se loguea y no propaga
     * excepción — nunca debe romper el flujo de guardado local.
     */
    suspend fun syncUp(context: Context, profileManager: UserProfileManager) {
        if (!ensureInit(context)) return
        val uid = ensureSignedIn() ?: return
        try {
            val history = profileManager.getHistory().map { p ->
                mapOf(
                    "name" to p.name,
                    "presetName" to p.presetName,
                    "timestamp" to p.timestamp,
                    "sourceApp" to p.sourceApp
                )
            }
            val doc = mapOf(
                "currentPreset" to profileManager.getCurrentPreset(),
                "history" to history,
                "updatedAt" to System.currentTimeMillis()
            )
            firestore?.collection("users")?.document(uid)?.set(doc)?.await()
            Log.i(TAG, "syncUp OK (${history.size} entradas de historial)")
        } catch (t: Throwable) {
            Log.e(TAG, "syncUp falló — se queda solo en local por ahora", t)
        }
    }

    /**
     * Trae el estado remoto y lo fusiona con el local: se queda con el que
     * tenga updatedAt/timestamp mas reciente por entrada (last-write-wins),
     * no pisa el historial local a ciegas. Best-effort igual que syncUp.
     */
    suspend fun syncDown(context: Context, profileManager: UserProfileManager) {
        if (!ensureInit(context)) return
        val uid = ensureSignedIn() ?: return
        try {
            val snap = firestore?.collection("users")?.document(uid)?.get()?.await() ?: return
            if (!snap.exists()) {
                Log.i(TAG, "syncDown: sin datos remotos todavia (primera vez)")
                return
            }
            val remoteHistory = (snap.get("history") as? List<*>)?.mapNotNull { raw ->
                val m = raw as? Map<*, *> ?: return@mapNotNull null
                val name = m["name"] as? String ?: return@mapNotNull null
                val presetName = m["presetName"] as? String ?: return@mapNotNull null
                val timestamp = (m["timestamp"] as? Number)?.toLong() ?: return@mapNotNull null
                val sourceApp = m["sourceApp"] as? String
                UserProfileManager.UserProfile(name, presetName, timestamp, sourceApp)
            } ?: emptyList()

            // Merge: union por timestamp, sin duplicar, ultimos 50, orden cronologico.
            val localHistory = profileManager.getHistory()
            val merged = (localHistory + remoteHistory)
                .distinctBy { it.timestamp to it.presetName }
                .sortedBy { it.timestamp }
                .takeLast(50)
            profileManager.replaceHistory(merged)
            Log.i(TAG, "syncDown OK — historial fusionado: ${localHistory.size} local + ${remoteHistory.size} remoto -> ${merged.size} final")
        } catch (t: Throwable) {
            Log.e(TAG, "syncDown falló — se sigue usando el estado local", t)
        }
    }
}
