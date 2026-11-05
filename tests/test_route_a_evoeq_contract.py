"""Contrato estructural: el EQ evolutivo (antes solo Ruta B) tambien corre en Ruta A.

Es una prueba de LECTURA de codigo: fija que ambos puntos de entrada de la cadena de la app
(nativeProcess y nativeProcessBlock) lo invocan justo tras g_eq, que la calibracion no corre en el
hilo de audio y que la UI lo controla. El comportamiento real esta en test_evolutionary_eq (host).
"""
import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JNI = open(os.path.join(ROOT, "app/src/main/cpp/jni/ivanna_omega_jni.cpp"), encoding="utf-8").read()
UI = open(os.path.join(ROOT, "app/src/main/java/com/ivanna/omega/ui/IvannaControlPanel.kt"), encoding="utf-8").read()


class RouteAEvolutionaryEqContract(unittest.TestCase):
    def test_both_route_a_entry_points_apply_it_right_after_the_eq(self):
        self.assertRegex(JNI, r"g_eq\.process\(g_ats\.chL, g_ats\.chR, n\);\s*\n\s*routeAApplyEvoEq\(g_ats\.chL, g_ats\.chR, n\)")
        self.assertRegex(JNI, r"g_eq\.process\(lBuf, rBuf, n\);\s*\n\s*routeAApplyEvoEq\(lBuf, rBuf, n\)")

    def test_calibration_runs_on_its_own_thread_never_in_the_audio_callback(self):
        body = re.search(r"static void routeAStartEvoEqCalibration\(\).*?\n\}", JNI, re.S).group(0)
        self.assertIn("pthread_create", body)
        self.assertIn("PTHREAD_CREATE_DETACHED", body)
        self.assertIn("routeAEvoEqCalibrationWorker", body)
        apply_body = re.search(r"static void routeAApplyEvoEq\(.*?\n\}", JNI, re.S).group(0)
        self.assertNotIn("calibrate", apply_body)
        self.assertNotIn("new ", apply_body)

    def test_only_a_verified_calibration_is_published(self):
        self.assertRegex(JNI, r"if \(eq->isReady\(\)\)\s*\{\s*g_evoEqA\.store\(eq, std::memory_order_release\)")

    def test_calibration_worker_clears_guard_after_success_or_failure(self):
        start = re.search(r"static void routeAStartEvoEqCalibration\(\).*?\n\}", JNI, re.S).group(0)
        worker = re.search(r"static void\* routeAEvoEqCalibrationWorker\(.*?\n\}", JNI, re.S).group(0)
        self.assertIn("pthread_attr_init", start)
        self.assertIn("pthread_create", start)
        self.assertIn("if (createRc != 0)", start)
        self.assertIn("g_evoEqACalibrating.store(false, std::memory_order_release);", start)
        self.assertIn("delete eq;", worker)
        self.assertIn("g_evoEqACalibrating.store(false, std::memory_order_release);", worker)

    def test_started_from_native_init_before_dsp_is_marked_ready(self):
        self.assertRegex(JNI, r"routeAStartEvoEqCalibration\(\);[^\n]*\n\s*g_initialized\.store\(true, std::memory_order_release\);")

    def test_ui_exposes_the_toggle(self):
        self.assertIn("EQ EVOLUTIVO 512", UI)

    def test_switch_is_persisted_and_restored_at_startup(self):
        prefs = open(os.path.join(ROOT, "app/src/main/java/com/ivanna/omega/core/EvolutionaryEqPrefs.kt"), encoding="utf-8").read()
        restorer = open(os.path.join(ROOT, "app/src/main/java/com/ivanna/omega/core/PersistedStateRestorer.kt"), encoding="utf-8").read()
        self.assertIn("nativeSetEvolutionaryEqEnabled", prefs)
        self.assertIn("putBoolean(KEY, enabled)", prefs)
        self.assertIn("EvolutionaryEqPrefs.restore(ctx)", restorer)
        # el panel guarda por el objeto (persiste + nativo), no llama al nativo directo
        self.assertIn("EvolutionaryEqPrefs.setEnabled(context, en)", UI)
        self.assertNotIn("nativeSetEvolutionaryEqEnabled", UI)

    def test_switch_is_not_painted_from_the_calibration_state(self):
        # nativeIsEvolutionaryEqActive() es false mientras calibra: pintaria "apagado" estando encendido
        block = re.search(r"var evoEqEnabled by remember.*?\n    \}?\n", UI, re.S)
        init = UI[UI.index("var evoEqEnabled by remember"):UI.index("var evoEqEnabled by remember") + 220]
        self.assertNotIn("nativeIsEvolutionaryEqActive", init)
        self.assertIn("EvolutionaryEqPrefs.isEnabled(context)", init)

    def test_calibration_readback_is_consumed_as_separate_live_status(self):
        self.assertIn("nativeIsEvolutionaryEqActive()", UI)
        self.assertIn("evoEqActive = runCatching", UI)
        self.assertIn("filtro calibrado, validado y activo", UI)
        self.assertIn("calibrando/verificando filtro", UI)


if __name__ == "__main__":
    unittest.main()


PB = open(os.path.join(ROOT, "app/src/main/java/com/ivanna/omega/neuromorphic/PiLstmBridge.kt"), encoding="utf-8").read()


class RouteANpeContract(unittest.TestCase):
    def test_both_entry_points_apply_npe_right_before_the_punch_and_limiter(self):
        self.assertRegex(JNI, r"routeAApplyNpe\(g_ats\.pdOutL, g_ats\.pdOutR, n\);[^\n]*\n\s*g_punch\.process\(g_ats\.pdOutL, g_ats\.pdOutR, n\)")
        self.assertRegex(JNI, r"routeAApplyNpe\(oL, oR, n\);[^\n]*\n\s*g_punch\.process\(oL, oR, n\)")

    def test_off_by_default_until_the_app_publishes_the_state(self):
        self.assertRegex(JNI, r"g_npeAOn\{false\}")

    def test_prepared_in_native_init_not_in_the_audio_callback(self):
        apply_body = re.search(r"static void routeAApplyNpe\(.*?\n\}", JNI, re.S).group(0)
        self.assertNotIn("prepare", apply_body)
        self.assertRegex(JNI, r"g_npeA\.prepare\(static_cast<float>\(sr\)\)")

    def test_app_state_reaches_the_native_chain_without_the_daemon(self):
        # applyLocalNpe corre antes del envio al daemon: la Ruta A no depende de que el modulo exista.
        self.assertRegex(PB, r"applyLocalNpe\(\)\s*\n\s*IvannaNativeLib\.nativeSetNpeModule\(")
