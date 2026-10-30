package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class RouteCapabilitiesTest {
    @Test fun noRootWarnsAndListsDaemonOnlyFeatures() {
        val l = RouteCapabilities.limitsFor(AudioBackendSelector.Mode.NO_ROOT)
        assertNotNull(l)
        assertEquals("Modo sin root", l!!.first)
        assertTrue(l.second.isNotEmpty())
    }

    @Test fun rootWithoutDaemonSaysTheModuleIsNotAnswering() {
        val l = RouteCapabilities.limitsFor(AudioBackendSelector.Mode.ROOT_NO_DAEMON)!!
        assertTrue(l.first.contains("daemon"))
    }

    @Test fun rootWithDaemonAndUnknownShowNothing() {
        assertNull(RouteCapabilities.limitsFor(AudioBackendSelector.Mode.ROOT_DAEMON))
        assertNull(RouteCapabilities.limitsFor(AudioBackendSelector.Mode.UNKNOWN))
    }

    @Test fun everyModeIsHandled() {
        AudioBackendSelector.Mode.values().forEach { RouteCapabilities.limitsFor(it) }
    }
}
