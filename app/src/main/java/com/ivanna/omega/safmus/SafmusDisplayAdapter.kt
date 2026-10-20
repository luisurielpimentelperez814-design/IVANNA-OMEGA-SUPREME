package com.ivanna.omega.safmus

import android.app.Activity
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.Display
import android.view.Window
import java.lang.ref.WeakReference
import kotlin.math.abs

/**
 * Window-scoped display adapter. Only changes the foreground IVANNA window's
 * preferred mode; never writes global settings or vendor display nodes.
 * The OS may decline the request, so actual refresh is observed separately.
 */
object SafmusDisplayAdapter {
    private const val TAG = "SAFMUS-DisplayAdapter"
    private val main = Handler(Looper.getMainLooper())
    @Volatile private var activeActivity = WeakReference<Activity>(null)
    private var originalPreferredModeId: Int? = null
    private var lastAppliedBudget: SafmusHardwareCoordinator.Budget? = null

    fun bind(activity: Activity) {
        runOnMain {
            if (activity.isFinishing || activity.isDestroyed) return@runOnMain
            val current = activeActivity.get()
            if (current !== activity) {
                restoreCurrent()
                activeActivity = WeakReference(activity)
                originalPreferredModeId = activity.window.attributes.preferredDisplayModeId
                lastAppliedBudget = null
            }
            apply(activity, SafmusHardwareCoordinator.lastSnapshot?.budget
                ?: SafmusHardwareCoordinator.Budget.NORMAL)
        }
    }

    fun applyActive(budget: SafmusHardwareCoordinator.Budget) {
        runOnMain {
            val activity = activeActivity.get() ?: return@runOnMain
            if (!activity.isFinishing && !activity.isDestroyed) apply(activity, budget)
        }
    }

    fun unbind(activity: Activity) {
        runOnMain {
            if (activeActivity.get() === activity) restoreCurrent()
        }
    }

    private fun apply(activity: Activity, budget: SafmusHardwareCoordinator.Budget) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) return
        val window = activity.window
        val display = window.decorView.display ?: return
        val modes = runCatching { display.supportedModes.toList() }.getOrDefault(emptyList())
        if (modes.isEmpty()) return

        val originalId = originalPreferredModeId ?: window.attributes.preferredDisplayModeId
        val currentMode = display.mode
        val target = when (budget) {
            SafmusHardwareCoordinator.Budget.NORMAL ->
                modes.firstOrNull { it.modeId == originalId }
                    ?: modes.minByOrNull { abs(it.refreshRate - currentMode.refreshRate) }
            SafmusHardwareCoordinator.Budget.CONSERVATIVE,
            SafmusHardwareCoordinator.Budget.SAFE ->
                modes.filter {
                    it.physicalWidth == currentMode.physicalWidth &&
                    it.physicalHeight == currentMode.physicalHeight &&
                    it.refreshRate <= currentMode.refreshRate
                }.minByOrNull { it.refreshRate }
        } ?: return

        if (lastAppliedBudget == budget &&
            window.attributes.preferredDisplayModeId == target.modeId) return
        val params = window.attributes
        params.preferredDisplayModeId = target.modeId
        window.attributes = params
        lastAppliedBudget = budget
        Log.i(TAG, "SAFMUS budget=$budget preferredMode=${target.modeId} targetHz=${target.refreshRate}; effectiveHz=${display.refreshRate}")
    }

    private fun restoreCurrent() {
        val activity = activeActivity.get()
        val original = originalPreferredModeId
        if (activity != null && original != null && !activity.isFinishing && !activity.isDestroyed &&
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            runCatching {
                val params = activity.window.attributes
                params.preferredDisplayModeId = original
                activity.window.attributes = params
            }
        }
        activeActivity.clear()
        originalPreferredModeId = null
        lastAppliedBudget = null
    }

    private fun runOnMain(block: () -> Unit) {
        if (Looper.myLooper() == Looper.getMainLooper()) block() else main.post(block)
    }
}
