package com.ivanna.omega.fotoholo

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SafmusRouteSupervisorTest {
    // FIX (2026-11-09, cero procesamiento en ambas rutas): la exclusividad de
    // Ruta B exige evidencia de procesamiento, no solo registro del efecto.
    @Test fun systemWideRouteTakesPriorityOnlyWithProcessingEvidence() {
        val confirmed = SafmusRouteSupervisor.decide(true, true, true, false, routeBProcessingConfirmed = true)
        assertEquals(SafmusRouteSupervisor.Route.ROUTE_B, confirmed.route)
        assertFalse(confirmed.allowSpectralAdaptation)
    }

    @Test fun unconfirmedSystemWideRouteDoesNotBlockRouteA() {
        // Efecto global registrado pero mudo (sin evidencia de procesamiento):
        // Ruta A sigue adaptando en vez de quedarse en silencio.
        val d = SafmusRouteSupervisor.decide(true, true, true, false, routeBProcessingConfirmed = false)
        assertEquals(SafmusRouteSupervisor.Route.ROUTE_A, d.route)
        assertTrue(d.allowSpectralAdaptation)
        // El default del parámetro también es fail-closed hacia A.
        val dDefault = SafmusRouteSupervisor.decide(true, true, true, false)
        assertEquals(SafmusRouteSupervisor.Route.ROUTE_A, dDefault.route)
        assertTrue(dDefault.allowSpectralAdaptation)
    }

    @Test fun routeAAdaptsOnlyWithValidSignalEnabledAndNoBypass() {
        assertTrue(SafmusRouteSupervisor.decide(false, true, true, false).allowSpectralAdaptation)
        assertFalse(SafmusRouteSupervisor.decide(false, false, true, false).allowSpectralAdaptation)
        assertFalse(SafmusRouteSupervisor.decide(false, true, false, false).allowSpectralAdaptation)
        assertFalse(SafmusRouteSupervisor.decide(false, true, true, true).allowSpectralAdaptation)
    }

    @Test fun evolutionProposalRequiresRouteSignalFiniteFitnessAndHeadroom() {
        assertTrue(SafmusRouteSupervisor.mayAcceptEvolutionProposal(
            SafmusRouteSupervisor.Route.ROUTE_A, true, 0.8f, true,
            candidateFiniteAndStable = true, latencyWithinBudget = true
        ))
        assertFalse(SafmusRouteSupervisor.mayAcceptEvolutionProposal(
            SafmusRouteSupervisor.Route.ROUTE_A, true, 0.8f, true
        )) // default evidence is absent: fail closed
        assertFalse(SafmusRouteSupervisor.mayAcceptEvolutionProposal(SafmusRouteSupervisor.Route.ROUTE_B, true, 0.8f, true, true, true))
        assertFalse(SafmusRouteSupervisor.mayAcceptEvolutionProposal(SafmusRouteSupervisor.Route.ROUTE_A, false, 0.8f, true, true, true))
        assertFalse(SafmusRouteSupervisor.mayAcceptEvolutionProposal(SafmusRouteSupervisor.Route.ROUTE_A, true, Float.NaN, true, true, true))
        assertFalse(SafmusRouteSupervisor.mayAcceptEvolutionProposal(SafmusRouteSupervisor.Route.ROUTE_A, true, 0.8f, false))
    }
}
