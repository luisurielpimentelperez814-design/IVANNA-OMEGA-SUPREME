/*
 * © 2026 Luis Uriel Pimentel Pérez — IVANNA N-P-E
 * LiveDiagnosticConnectorTest.kt — Puente de diagnóstico en Floot (v2.5.443).
 *
 * Prueba la resolución pura del endpoint: default a Floot, migración
 * automática de la URL vieja de Render, y respeto a una URL propia.
 * (Las funciones con Context/SharedPreferences no se prueban aquí: la
 * resolución es la lógica que decide a dónde apunta la app.)
 */
package com.ivanna.omega.core

import org.junit.Assert.assertEquals
import org.junit.Test

class LiveDiagnosticConnectorTest {

    @Test fun defaultIsFloot() {
        assertEquals(
            "https://ivanna-diagnostic-bridge.floot.app/_api/v1/diagnostics",
            LiveDiagnosticConnector.DEFAULT_ENDPOINT
        )
        assertEquals(
            LiveDiagnosticConnector.DEFAULT_ENDPOINT,
            LiveDiagnosticConnector.resolveEndpoint(null)
        )
        assertEquals(
            LiveDiagnosticConnector.DEFAULT_ENDPOINT,
            LiveDiagnosticConnector.resolveEndpoint("")
        )
        assertEquals(
            LiveDiagnosticConnector.DEFAULT_ENDPOINT,
            LiveDiagnosticConnector.resolveEndpoint("   ")
        )
    }

    @Test fun legacyRenderMigratesToFloot() {
        assertEquals(
            LiveDiagnosticConnector.DEFAULT_ENDPOINT,
            LiveDiagnosticConnector.resolveEndpoint(
                "https://ivanna-diagnostic-bridge.onrender.com/v1/diagnostics"
            )
        )
        // Con barra final también migra.
        assertEquals(
            LiveDiagnosticConnector.DEFAULT_ENDPOINT,
            LiveDiagnosticConnector.resolveEndpoint(
                "https://ivanna-diagnostic-bridge.onrender.com/v1/diagnostics/"
            )
        )
    }

    @Test fun customEndpointIsRespected() {
        val custom = "https://mi-puente.example.com/v1/diagnostics"
        assertEquals(custom, LiveDiagnosticConnector.resolveEndpoint(custom))
        assertEquals(custom, LiveDiagnosticConnector.resolveEndpoint("$custom/"))
    }

    @Test fun flootEndpointPassesThrough() {
        assertEquals(
            LiveDiagnosticConnector.DEFAULT_ENDPOINT,
            LiveDiagnosticConnector.resolveEndpoint(LiveDiagnosticConnector.DEFAULT_ENDPOINT)
        )
    }
}
