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
        self.assertIn("std::thread", body)
        self.assertIn(".detach()", body)
        apply_body = re.search(r"static void routeAApplyEvoEq\(.*?\n\}", JNI, re.S).group(0)
        self.assertNotIn("calibrate", apply_body)
        self.assertNotIn("new ", apply_body)

    def test_only_a_verified_calibration_is_published(self):
        self.assertRegex(JNI, r"if \(eq->isReady\(\)\) g_evoEqA\.store\(eq, std::memory_order_release\)")

    def test_started_from_native_init_before_dsp_is_marked_ready(self):
        self.assertRegex(JNI, r"routeAStartEvoEqCalibration\(\);[^\n]*\n\s*g_initialized\.store\(true, std::memory_order_release\);")

    def test_ui_exposes_the_toggle(self):
        self.assertIn("nativeSetEvolutionaryEqEnabled", UI)
        self.assertIn("EQ EVOLUTIVO 512", UI)


if __name__ == "__main__":
    unittest.main()
