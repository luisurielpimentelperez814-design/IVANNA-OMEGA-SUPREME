package com.ivanna.omega.spatial

import org.junit.Assert.*
import org.junit.Test

class AbxTrialSessionTest {
    @Test fun redrawsOnEachTrialAndRequiresAllSamples() {
        val draws = ArrayDeque(listOf(false, true, true, false))
        val session = AbxTrialSession { draws.removeFirst() }
        for (expected in listOf(1, 2, 2, 1)) {
            session.begin()
            assertEquals(expected, session.scenario("X"))
            session.confirm("A", true)
            session.confirm("B", true)
            assertFalse(session.canSubmit("A"))
            session.confirm("X", true)
            assertTrue(session.canSubmit("B"))
            assertTrue(session.finish(if (expected == 1) "A" else "B"))
            assertFalse(session.canSubmit("B"))
        }
        assertTrue(draws.isEmpty())
    }
    @Test fun rejectedOrCancelledSampleCannotBeRecorded() {
        val session = AbxTrialSession { true }
        session.begin()
        session.confirm("A", true)
        session.confirm("B", false)
        assertFalse(session.active)
        assertFalse(session.canSubmit("B"))
        session.begin()
        session.cancel()
        assertFalse(session.canSubmit("A"))
    }
}
