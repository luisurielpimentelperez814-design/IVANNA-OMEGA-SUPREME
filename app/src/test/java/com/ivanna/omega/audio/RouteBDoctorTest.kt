/*
 * © 2026 Luis Uriel Pimentel Pérez — IVANNA N-P-E
 * RouteBDoctorTest.kt — Doctor de auto-reparación de Ruta B (v2.5.443).
 *
 * Prueba las funciones PURAS del doctor (sin I/O, sin root, sin LLM):
 * parseo del log, diagnóstico de cada puerta, whitelist de reparaciones,
 * JSON de diagnóstico y parseo del consejo LLM. El ciclo con estado
 * (contadores, verificación) se prueba vía runCycle con evidencia sintética.
 */
package com.ivanna.omega.audio

import com.ivanna.omega.dsp.DSPBridge
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class RouteBDoctorTest {

    private fun baseEvidence(): RouteBEvidence = RouteBEvidence(
        attachedSessionId = 329,
        discoveredSessionId = 329,
        daemonReachable = true,
        effectAttached = true,
        beaconFrames = 48000L,
        effectState = DSPBridge.EffectState.PROCESSING,
        beaconFileExists = true,
        fusionActive = true,
        psiActive = true,
        ratio = 1.0f,
        musicPlaying = true,
        logGates = emptySet(),
        lastAttachFailure = OmegaFailureKind.UNKNOWN,
        beaconNeverSeenCycles = 0,
        frozenCycles = 0
    )

    // ── parseo del log ───────────────────────────────────────────────────────

    @Test fun logParseNamesEachGate() {
        val lines = listOf(
            "11-13 10:19:00.000  1646  1646 W IvannaOmegaEffect: omega_process passthrough: canales != 2 (no estereo)",
            "11-13 10:19:05.000  1646  1646 W IvannaOmegaEffect: omega_process passthrough: integrity_self_check FALLO (ruta de .so no legitima)",
            "11-13 10:19:10.000  1646  1646 W IvannaOmegaEffect: omega_process passthrough: antitamper LOCKDOWN (debugger/hook/emulador en audioserver)",
            "11-13 10:19:15.000  1646  1646 W IvannaOmegaEffect: omega_process passthrough: fusionCore NULO (motor no inicializado)",
            "11-13 10:19:20.000  1646  1646 W IvannaOmegaEffect: omega_process passthrough: !enabled && bypass silencioso (efecto no habilitado a nivel nativo)",
            "11-13 10:19:25.000  1646  1646 E IvannaOmegaEffect: omega_process: FusionEngine lazy FALLO (nothrow null) — passthrough"
        )
        val gates = parseOmegaEffectLog(lines)
        assertTrue(gates.contains(RouteBGate.NOT_STEREO))
        assertTrue(gates.contains(RouteBGate.INTEGRITY_FAIL))
        assertTrue(gates.contains(RouteBGate.ANTITAMPER_LOCKDOWN))
        assertTrue(gates.contains(RouteBGate.FUSION_CORE_NULL))
        assertTrue(gates.contains(RouteBGate.NOT_ENABLED_NATIVE))
    }

    @Test fun logParseBeaconOpenFailedIsFileMissing() {
        val lines = listOf(
            "11-13 10:19:00.000  1646  1646 W OmegaCtrlBus: beacon open(/data/local/tmp/omega_effect_beacon) failed: Permission denied"
        )
        val gates = parseOmegaEffectLog(lines)
        assertEquals(setOf(RouteBGate.BEACON_FILE_MISSING), gates)
    }

    @Test fun logParseIgnoresUnrelatedLines() {
        val lines = listOf(
            "11-13 10:19:00.000  1646  1646 I IvannaOmegaEffect: omega_process: FusionEngine creado lazy (1er bloque)",
            "11-13 10:19:00.000  1646  1646 I IvannaOmegaEffect: omega_process: auto-enable (EFFECT_CMD_ENABLE no llego del HAL; beacon onEnable)",
            "11-13 10:19:00.000  1646  1646 D AudioFlinger: something else entirely"
        )
        assertTrue(parseOmegaEffectLog(lines).isEmpty())
    }

    // ── diagnóstico ──────────────────────────────────────────────────────────

    @Test fun diagnoseHealthyWhenProcessing() {
        val d = diagnoseRouteB(baseEvidence())
        assertEquals(RouteBGate.HEALTHY_PROCESSING, d.gate)
        assertEquals(RouteBRepair.NONE, decideRouteBRepair(d))
    }

    @Test fun diagnoseDaemonUnreachable() {
        val d = diagnoseRouteB(baseEvidence().copy(daemonReachable = false, effectState = DSPBridge.EffectState.UNAVAILABLE))
        assertEquals(RouteBGate.DAEMON_UNREACHABLE, d.gate)
        assertEquals(RouteBRepair.REPORT_ONLY, decideRouteBRepair(d))
    }

    @Test fun diagnoseNoSessionDiscovered() {
        val d = diagnoseRouteB(
            baseEvidence().copy(
                discoveredSessionId = 0, attachedSessionId = 0, effectAttached = false,
                effectState = DSPBridge.EffectState.NO_EFFECT, beaconFrames = -1, musicPlaying = false
            )
        )
        assertEquals(RouteBGate.NO_SESSION_DISCOVERED, d.gate)
        assertEquals(RouteBRepair.REPORT_ONLY, decideRouteBRepair(d))
    }

    @Test fun diagnoseNotAttached() {
        val d = diagnoseRouteB(
            baseEvidence().copy(
                effectAttached = false, effectState = DSPBridge.EffectState.NO_EFFECT,
                beaconFrames = -1, lastAttachFailure = OmegaFailureKind.INVALID_OPERATION_38
            )
        )
        assertEquals(RouteBGate.NOT_ATTACHED, d.gate)
        assertEquals(RouteBRepair.RETRY_ATTACH, decideRouteBRepair(d))
        assertTrue(d.explanation.contains("INVALID_OPERATION_38"))
    }

    @Test fun diagnoseIntegrityFailBeatsBeacon() {
        // El log manda sobre el beacon: aunque el beacon esté ausente, la
        // puerta es la integridad (el beacon ausente es consecuencia).
        val d = diagnoseRouteB(
            baseEvidence().copy(
                effectState = DSPBridge.EffectState.ENABLED_IDLE, beaconFrames = -1,
                logGates = setOf(RouteBGate.INTEGRITY_FAIL)
            )
        )
        assertEquals(RouteBGate.INTEGRITY_FAIL, d.gate)
        assertEquals(RouteBRepair.REPORT_ONLY, decideRouteBRepair(d))
    }

    @Test fun diagnoseAntitamperLockdown() {
        val d = diagnoseRouteB(
            baseEvidence().copy(
                effectState = DSPBridge.EffectState.ENABLED_IDLE, beaconFrames = -1,
                logGates = setOf(RouteBGate.ANTITAMPER_LOCKDOWN)
            )
        )
        assertEquals(RouteBGate.ANTITAMPER_LOCKDOWN, d.gate)
        assertEquals(RouteBRepair.REPORT_ONLY, decideRouteBRepair(d))
    }

    @Test fun diagnoseNotStereoIsCorrectBehavior() {
        val d = diagnoseRouteB(
            baseEvidence().copy(
                effectState = DSPBridge.EffectState.ENABLED_IDLE, beaconFrames = 0,
                logGates = setOf(RouteBGate.NOT_STEREO)
            )
        )
        assertEquals(RouteBGate.NOT_STEREO, d.gate)
        // No es un bug: no se repara, se reporta.
        assertEquals(RouteBRepair.REPORT_ONLY, decideRouteBRepair(d))
    }

    @Test fun diagnoseFusionCoreNullRecreates() {
        val d = diagnoseRouteB(
            baseEvidence().copy(
                effectState = DSPBridge.EffectState.ENABLED_IDLE, beaconFrames = -1,
                logGates = setOf(RouteBGate.FUSION_CORE_NULL)
            )
        )
        assertEquals(RouteBGate.FUSION_CORE_NULL, d.gate)
        assertEquals(RouteBRepair.RECREATE_ORPHAN, decideRouteBRepair(d))
    }

    @Test fun diagnoseNotEnabledNativeRecreates() {
        val d = diagnoseRouteB(
            baseEvidence().copy(
                effectState = DSPBridge.EffectState.ENABLED_IDLE, beaconFrames = -1,
                logGates = setOf(RouteBGate.NOT_ENABLED_NATIVE)
            )
        )
        assertEquals(RouteBGate.NOT_ENABLED_NATIVE, d.gate)
        assertEquals(RouteBRepair.RECREATE_ORPHAN, decideRouteBRepair(d))
    }

    @Test fun diagnoseBeaconFileMissingPrecreates() {
        val d = diagnoseRouteB(
            baseEvidence().copy(
                effectState = DSPBridge.EffectState.ENABLED_IDLE, beaconFrames = -1,
                beaconFileExists = false
            )
        )
        assertEquals(RouteBGate.BEACON_FILE_MISSING, d.gate)
        assertEquals(RouteBRepair.PRECREATE_BEACON, decideRouteBRepair(d))
    }

    @Test fun diagnoseBeaconNeverExistedIsTheBlindSpot() {
        // PUNTO CIEGO del detector viejo: beacon -1 durante 3 ciclos
        // mientras está acoplado y suena. Antes se conservaba el estado y
        // nunca se recreaba; el doctor lo diagnostica y lo recrea.
        val d = diagnoseRouteB(
            baseEvidence().copy(
                effectState = DSPBridge.EffectState.ENABLED_IDLE, beaconFrames = -1,
                beaconFileExists = true, beaconNeverSeenCycles = 3
            )
        )
        assertEquals(RouteBGate.BEACON_NEVER_EXISTED, d.gate)
        assertEquals(RouteBRepair.RECREATE_ORPHAN, decideRouteBRepair(d))
    }

    @Test fun diagnoseBeaconNeverExistedNeedsThreeCycles() {
        // Con solo 2 ciclos sin beacon aún no se declara (fail-closed).
        val d = diagnoseRouteB(
            baseEvidence().copy(
                effectState = DSPBridge.EffectState.ENABLED_IDLE, beaconFrames = -1,
                beaconFileExists = true, beaconNeverSeenCycles = 2
            )
        )
        assertEquals(RouteBGate.UNKNOWN, d.gate)
        assertEquals(RouteBRepair.REPORT_ONLY, decideRouteBRepair(d))
    }

    @Test fun diagnoseOrphanFrozenRecreates() {
        val d = diagnoseRouteB(
            baseEvidence().copy(
                effectState = DSPBridge.EffectState.ENABLED_IDLE, beaconFrames = 96000L,
                frozenCycles = 3
            )
        )
        assertEquals(RouteBGate.ORPHAN_FROZEN, d.gate)
        assertEquals(RouteBRepair.RECREATE_ORPHAN, decideRouteBRepair(d))
    }

    @Test fun diagnoseOrphanFrozenNeedsThreeCycles() {
        val d = diagnoseRouteB(
            baseEvidence().copy(
                effectState = DSPBridge.EffectState.ENABLED_IDLE, beaconFrames = 96000L,
                frozenCycles = 2
            )
        )
        assertEquals(RouteBGate.EFFECT_IDLE, d.gate)
        assertEquals(RouteBRepair.NONE, decideRouteBRepair(d))
    }

    @Test fun diagnosePausedIsNotOrphan() {
        // En pausa los frames congelados son lo esperado: no es orfandad.
        val d = diagnoseRouteB(
            baseEvidence().copy(
                effectState = DSPBridge.EffectState.ENABLED_IDLE, beaconFrames = 96000L,
                musicPlaying = false, frozenCycles = 5
            )
        )
        assertEquals(RouteBGate.EFFECT_IDLE, d.gate)
        assertEquals(RouteBRepair.NONE, decideRouteBRepair(d))
    }

    // ── JSON de diagnóstico (solo datos, nunca código) ───────────────────────

    @Test fun diagnosisJsonHasOnlyDiagnosticData() {
        val evidence = baseEvidence()
        val diagnosis = diagnoseRouteB(evidence)
        val json = routeBDiagnosisToJson(evidence, diagnosis)
        assertTrue(json.contains("HEALTHY_PROCESSING"))
        assertTrue(json.contains("\"attachedSessionId\":329"))
        assertTrue(json.contains("\"beaconFrames\":48000"))
        // No hay código fuente aquí: solo campos de diagnóstico.
        assertFalse(json.contains("fun "))
        assertFalse(json.contains("class "))
        assertFalse(json.contains("import "))
    }

    // ── consejo LLM ──────────────────────────────────────────────────────────

    @Test fun parseLlmAdviceExtractsGateAndRepair() {
        val text = "1) causa: FUSION_CORE_NULL\n2) el motor no se creó en el primer bloque\n3) acción: RECREATE_ORPHAN"
        val parsed = parseLlmAdvice(text)
        assertEquals(RouteBGate.FUSION_CORE_NULL to RouteBRepair.RECREATE_ORPHAN, parsed)
    }

    @Test fun parseLlmAdviceNullWhenNoCleanMatch() {
        assertNull(parseLlmAdvice("No lo sé, habría que mirar más cosas."))
        // Puerta sin reparación: no hay acción clara.
        assertNull(parseLlmAdvice("La causa es ORPHAN_FROZEN pero no sé qué hacer."))
    }

    @Test fun llmPromptAsksForWhitelistOnly() {
        val prompt = routeBDoctorLlmPrompt("{\"gate\":\"UNKNOWN\"}")
        assertTrue(prompt.contains("PRECREATE_BEACON"))
        assertTrue(prompt.contains("RECREATE_ORPHAN"))
        assertTrue(prompt.contains("REPORT_ONLY"))
        assertTrue(prompt.contains("sin código fuente"))
    }

    // ── ciclo del doctor (estado entre ciclos) ───────────────────────────────

    @Test fun doctorCycleCountsFrozenFramesAndDiagnosesOrphan() {
        val doctor = RouteBDoctor(logReader = { emptySet() })
        // 3 ciclos con los mismos frames mientras suena → huérfano.
        var outcome: RouteBDoctorOutcome? = null
        repeat(4) {
            outcome = doctor.runCycle(
                baseEvidence().copy(
                    effectState = DSPBridge.EffectState.ENABLED_IDLE, beaconFrames = 96000L
                )
            )
        }
        assertEquals(RouteBGate.ORPHAN_FROZEN, outcome!!.diagnosis.gate)
        assertEquals(RouteBRepair.RECREATE_ORPHAN, outcome!!.repair)
    }

    @Test fun doctorCycleCountsNeverSeenBeacon() {
        val doctor = RouteBDoctor(logReader = { emptySet() })
        var outcome: RouteBDoctorOutcome? = null
        repeat(4) {
            outcome = doctor.runCycle(
                baseEvidence().copy(
                    effectState = DSPBridge.EffectState.ENABLED_IDLE, beaconFrames = -1L,
                    beaconFileExists = true
                )
            )
        }
        assertEquals(RouteBGate.BEACON_NEVER_EXISTED, outcome!!.diagnosis.gate)
        assertEquals(RouteBRepair.RECREATE_ORPHAN, outcome!!.repair)
    }

    @Test fun doctorCycleResetsCountersWhenFramesGrow() {
        val doctor = RouteBDoctor(logReader = { emptySet() })
        // 2 ciclos congelados…
        repeat(2) {
            doctor.runCycle(
                baseEvidence().copy(
                    effectState = DSPBridge.EffectState.ENABLED_IDLE, beaconFrames = 96000L
                )
            )
        }
        // …y los frames crecen: no es huérfano, los contadores se resetean.
        val outcome = doctor.runCycle(
            baseEvidence().copy(beaconFrames = 144000L)
        )
        assertEquals(RouteBGate.HEALTHY_PROCESSING, outcome.diagnosis.gate)
        assertEquals(RouteBRepair.NONE, outcome.repair)
    }
}
