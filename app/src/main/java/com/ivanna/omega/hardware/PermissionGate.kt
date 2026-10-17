package com.ivanna.omega.hardware

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.provider.Settings
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat

/**
 * Adaptador Android de [PermissionPolicy]. No pide nada por sí mismo: solo responde "en qué estado está
 * este permiso" y guarda que ya se pidió. El diálogo lo lanza la pantalla cuando el usuario activa la función.
 */
object PermissionGate {
    private const val PREFS = "ivanna_permission_gate"

    fun isGranted(context: Context, permission: String): Boolean =
        ContextCompat.checkSelfPermission(context, permission) == PackageManager.PERMISSION_GRANTED

    fun markRequested(context: Context, permission: String) {
        runCatching {
            context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .edit().putBoolean(permission, true).apply()
        }
    }

    private fun wasRequested(context: Context, permission: String): Boolean =
        runCatching {
            context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .getBoolean(permission, false)
        }.getOrDefault(false)

    fun stateFor(context: Context, permission: String): PermissionState {
        val activity = findActivity(context)
        val rationale = activity != null &&
            runCatching { ActivityCompat.shouldShowRequestPermissionRationale(activity, permission) }
                .getOrDefault(false)
        return PermissionPolicy.decide(
            granted = isGranted(context, permission),
            shouldShowRationale = rationale,
            requestedBefore = wasRequested(context, permission),
        )
    }

    /** Abre la ficha de la app en Ajustes del sistema (único camino cuando el permiso está denegado para siempre). */
    fun openAppSettings(context: Context) {
        runCatching {
            context.startActivity(
                Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.fromParts("package", context.packageName, null))
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            )
        }
    }

    private fun findActivity(context: Context): Activity? {
        var c: Context? = context
        while (c is ContextWrapper) {
            if (c is Activity) return c
            c = c.baseContext
        }
        return null
    }
}
