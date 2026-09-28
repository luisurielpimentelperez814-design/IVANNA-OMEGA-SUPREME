package com.ivanna.omega.assistant

import android.content.Context
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch

/**
 * IvannaLaunchGreeting — bienvenida hablada al abrir la app (una sola vez
 * por proceso).
 *
 * Vive separado de IvannaAssistant/IvannaAssistantViewModel a propósito:
 * esas clases instancian micrófono + agente + memoria completos, y solo se
 * crean cuando el usuario entra al panel del asistente. Este saludo debe
 * sonar apenas se acepta el aviso legal y se entra al dashboard, sin
 * arrancar el resto de la maquinaria conversacional — así que trae su
 * propio IvannaVoiceEngine, ligero y desechable.
 *
 * greetOnce() espera a VoiceState.READY antes de hablar (el motor TTS del
 * sistema inicializa async) y usa una de varias líneas para no sonar
 * idéntica cada vez que se abre la app.
 */
object IvannaLaunchGreeting {

    private const val TAG = "IvannaLaunchGreeting"

    @Volatile private var hasGreeted = false
    @Volatile private var engine: IvannaVoiceEngine? = null
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    private val welcomeLines = listOf(
        "Hola. Soy IVANNA, tu arquitecta de audio. El motor acústico ya está en línea, todo listo para escuchar como se debe.",
        "Bienvenido de vuelta. IVANNA en línea, con el kernel de audio calibrado. Dime qué escuchamos hoy.",
        "Hola, qué gusto tenerte de vuelta. Soy IVANNA. El pipeline está estable y en verde, estoy lista cuando tú lo estés."
    )

    /** Llamar desde el primer composable real tras aceptar el aviso legal. */
    fun greetOnce(context: Context) {
        if (hasGreeted) return
        hasGreeted = true
        val eng = engine ?: IvannaVoiceEngine(context.applicationContext).also { engine = it }
        scope.launch {
            runCatching {
                eng.state.first { it == VoiceState.READY }
                eng.speakWithIntent(welcomeLines.random(), IvannaVoiceEngine.IntentTone.AFFIRMATION)
            }.onFailure { Log.w(TAG, "Saludo de bienvenida falló: ${it.message}") }
        }
    }

    /** Solo para pruebas/depuración: permite forzar el saludo de nuevo. */
    fun resetForTesting() { hasGreeted = false }
}
