package com.ivanna.omega.fotoholo

import com.ivanna.omega.safmus.SafmusUniversalKernel

/**
 * Compatibility adapter for FotoHolo. All policy decisions live in the shared
 * SAFMUS kernel so additional controls and DSP paths can reuse the same gates.
 * SAFMUS is deterministic supervision, not a trained or perceptually validated model.
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
    ): Decision {
        val d = SafmusUniversalKernel.decideRoute(
            SafmusUniversalKernel.RouteEvidence(
                systemWideEffectActive = systemWideEffectActive,
                routeASignalValid = routeAHasValidSignal,
                routeAFrameFresh = routeAHasValidSignal,
                enabled = enabled,
                bypass = bypass
            )
        )
        return Decision(
            route = when (d.route) {
                SafmusUniversalKernel.Route.ROUTE_A -> Route.ROUTE_A
                SafmusUniversalKernel.Route.ROUTE_B -> Route.ROUTE_B
                SafmusUniversalKernel.Route.NONE -> Route.NONE
            },
            allowSpectralAdaptation = d.allowSpectralAdaptation,
            reason = d.reason
        )
    }

    /** Fitness alone cannot authorize activation; Route A, valid signal and safety gates are required. */
    fun mayAcceptEvolutionProposal(
        route: Route,
        signalValid: Boolean,
        fitness: Float,
        headroomValid: Boolean,
    ): Boolean = SafmusUniversalKernel.mayAcceptEvolutionProposal(
        route = when (route) {
            Route.ROUTE_A -> SafmusUniversalKernel.Route.ROUTE_A
            Route.ROUTE_B -> SafmusUniversalKernel.Route.ROUTE_B
            Route.NONE -> SafmusUniversalKernel.Route.NONE
        },
        signalValid = signalValid,
        fitness = fitness,
        headroomValid = headroomValid
    )
}
