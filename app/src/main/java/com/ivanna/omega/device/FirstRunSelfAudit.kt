package com.ivanna.omega.device

import android.content.Context
import android.util.Log

/**
 * Auto-auditoría de primera instalación de IVANNA.
 *
 * Flujo (sin reinicio, sin permisos nuevos, sin tocar el HAL):
 *   1. [DeviceProfiler] recolecta el perfil del equipo.
 *   2. [DeviceAuditor] lo compara con la [HardwareKnowledgeBase].
 *   3. [HotOptimizer] formula el plan y aplica lo aplicable en caliente,
 *      con rollback automático ante regresión medida.
 *   4. Se guarda el perfil + el informe (SharedPreferences) y se devuelve
 *      un resumen legible para la UI y el log.
 *
 * Idempotente: si ya se auditó esta versión de la base de conocimiento,
 * [runIfNeeded] no repite el trabajo (devuelve el resumen guardado). Se
 * puede forzar con [runForced] (p. ej. desde el panel de diagnóstico).
 *
 * ═══ PUNTO DE INTEGRACIÓN (para aplicar en IVANNAApplication.kt) ═══
 * Archivo: app/src/main/java/com/ivanna/omega/core/IVANNAApplication.kt
 * Lugar: dentro de onCreate(), DESPUÉS de la verificación de IntegrityGate
 * y DESPUÉS de IvannaControlLoop.start(), JUNTO a la línea que arranca
 * SafmusHardwareCoordinator (hoy ~línea 334):
 *
 *     runCatching { com.ivanna.omega.device.FirstRunSelfAudit.runIfNeeded(this) }
 *         .onFailure { Log.w(TAG, "Auto-auditoría de dispositivo falló (no fatal): ${it.message}") }
 *
 * Por qué ahí: IntegrityGate ya decidió si el DSP puede arrancar (fail-closed
 * antes que nada); el control loop ya corre; el coordinador SAFMUS ya publica
 * su presupuesto. La auto-auditoría es de solo lectura + optimizaciones
 * fail-closed, así que no puede romper el arranque: si algo falla, el
 * runCatching la traga y la app sigue con los valores por defecto.
 * NO se llama antes de super.onCreate() ni antes de IntegrityGate.
 * ═══════════════════════════════════════════════════════════════════════
 */
object FirstRunSelfAudit {
    private const val TAG = "IVANNA-FirstRunAudit"
    private const val PREFS = "ivanna_device_self_audit"
    private const val KEY_KB_VERSION = "kb_version"
    private const val KEY_FINGERPRINT = "fingerprint"
    private const val KEY_SUMMARY = "summary"
    private const val KEY_APPLIED = "applied_count"

    data class Outcome(
        val profile: DeviceProfile,
        val report: AuditReport,
        val results: List<HotOptimizer.ApplyResult>,
        /** Resumen legible para la UI. */
        val summary: String,
        /** true si esta corrida hizo el trabajo (false si ya estaba auditado). */
        val fresh: Boolean,
    )

    /** Corre la auto-auditoría solo si la base de conocimiento cambió o nunca se auditó. Devuelve null si ya estaba auditado (registra el resumen guardado en el log). */
    fun runIfNeeded(context: Context): Outcome? {
        val prefs = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val doneKb = prefs.getInt(KEY_KB_VERSION, -1)
        if (doneKb == HardwareKnowledgeBase.VERSION) {
            val saved = prefs.getString(KEY_SUMMARY, null)
            Log.i(TAG, "Dispositivo ya auditado con base v$doneKb; sin repetición.")
            if (saved != null) {
                // Sin perfil en memoria: se devuelve el resumen guardado vía
                // un Outcome sin re-perfilar; el llamador solo lo muestra.
                Log.i(TAG, saved)
            }
            return null
        }
        return runForced(context)
    }

    /** Corre la auto-auditoría completa aunque ya se hubiera auditado. */
    fun runForced(context: Context): Outcome {
        val app = context.applicationContext
        val profile = runCatching { DeviceProfiler.profile(app) }.getOrElse {
            throw IllegalStateException("No se pudo perfilar el dispositivo: ${it.message}", it)
        }
        DeviceProfiler.logProfile(profile)
        val report = DeviceAuditor.audit(profile)
        val plan = HotOptimizer.plan(report)
        val results = HotOptimizer.execute(plan)
        val applied = results.count { it.status == HotOptimizer.ApplyStatus.APPLIED }
        val unavailable = results.count { it.status == HotOptimizer.ApplyStatus.UNAVAILABLE }
        val reverted = results.count { it.status == HotOptimizer.ApplyStatus.REVERTED }
        val summary = buildString {
            append("Dispositivo auditado: ${report.match.entry.marketingName} (${report.match.entry.socModel}) · ")
            append("${report.bottlenecks.size} cuellos de botella · ")
            append("$applied optimizaciones aplicadas")
            if (unavailable > 0) append(", $unavailable no disponibles en este equipo")
            if (reverted > 0) append(", $reverted revertidas por regresión medida")
            append(" · latencia esperada ${report.match.entry.config.expectedLatencyMs} ms")
        }
        Log.i(TAG, summary)
        report.describe().forEach { Log.i(TAG, it) }
        results.forEach { Log.i(TAG, "Optimización ${it.kind}: ${it.status} — ${it.detail}") }
        app.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putInt(KEY_KB_VERSION, HardwareKnowledgeBase.VERSION)
            .putString(KEY_FINGERPRINT, profile.fingerprint())
            .putString(KEY_SUMMARY, summary)
            .putInt(KEY_APPLIED, applied)
            .apply()
        return Outcome(profile, report, results, summary, fresh = true)
    }

    /** Resumen guardado de la última auditoría, o null si nunca se auditó. */
    fun lastSummary(context: Context): String? =
        context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .getString(KEY_SUMMARY, null)
}
