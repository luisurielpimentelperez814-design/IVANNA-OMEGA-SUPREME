package com.ivanna.omega.fotoholo

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SafmusRouteSupervisorTest {
    @Test fun systemWideRouteTakesPriorityAndNeverEnablesParallelAdaptation() {
        val d = SafmusRouteSupervisor.decide(true, true, true, false)
        assertEquals(SafmusRouteSupervisor.Route.ROUTE_B, d.route)
        assertFalse(d.allowSpectralAdaptation)
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
