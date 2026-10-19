package com.ivanna.omega.fotoholo

/**
 * SAFMUS runtime guard for FotoHolo: deterministic route supervision, not a trained model.
 * Route B has priority when the system-wide effect is registered; Route A may adapt only
 * when B is absent and a fresh, valid in-process Bark frame exists. Otherwise abstain.
 */
object SafmusRouteSupervisor {
    enum class Route { ROUTE_A, ROUTE_B, NONE }

    data class Decision(
        val route: Route,
        val allowSpectralAdaptation: Boolean,
        val reason: String,
    )

    fun decide(
        systemWideEffectActive: Boolean,
        routeAHasValidSignal: Boolean,
        enabled: Boolean,
        bypass: Boolean,
    ): Decision = when {
        systemWideEffectActive -> Decision(
            Route.ROUTE_B, false,
            "Ruta B global activa; no duplicar procesamiento en Ruta A"
        )
        routeAHasValidSignal && enabled && !bypass -> Decision(
            Route.ROUTE_A, true,
            "Ruta A con señal válida; modulación adaptativa acotada permitida"
        )
        routeAHasValidSignal -> Decision(
            Route.ROUTE_A, false,
            if (bypass) "Bypass activo; conservar el ancho base"
            else "Señal válida; modulación desactivada"
        )
        else -> Decision(
            Route.NONE, false,
            "Sin ruta procesable con evidencia válida; conservar estado seguro"
        )
    }

    /** Fitness is only an optimizer score; without route + valid signal it cannot authorize activation. */
    fun mayAcceptEvolutionProposal(
        route: Route,
        signalValid: Boolean,
        fitness: Float,
        headroomValid: Boolean,
    ): Boolean = route == Route.ROUTE_A &&
        signalValid &&
        fitness.isFinite() &&
        headroomValid
}
