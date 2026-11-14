"""Tests de _count_ctest_targets (propuesta 0004).

Correcciones al draft del LLM al integrar (su 1ª pasada dio ROJO en el
validador scratch: `git apply --check` falló con "corrupt patch", y su
código tenía 3 defectos que su propio test no habría cazado):
- El contador incluía el nombre fantasma `name` de la línea de definición
  `function(ivanna_add_test name)` (el regex lo casa); se excluyen las
  líneas que empiezan por `function(`.
- El test usaba funciones sueltas estilo pytest, que `unittest discover`
  no recoge (0 tests); se reescribió como `unittest.TestCase`.
- El test cargaba el motor con `parents[2]` (eso es `tools/`, no la raíz)
  e importaba `shlex` sin usarlo; se usa el patrón de
  test_scan_saf_guards.py (sys.path a `tools/evolution/`, import top-level).
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import evolution_engine  # noqa: E402


class TestCountCtestTargets(unittest.TestCase):
    def test_counts_helper_invocations_and_direct_add_tests(self):
        cmake = """
    function(ivanna_add_test name)
      add_test(NAME ${name} COMMAND $<TARGET_FILE:${name}>)
      gtest_discover_tests(${name})
    endfunction()

    ivanna_add_test(foo)
    ivanna_add_test(bar)
    add_test(NAME direct_one COMMAND direct_one)
    add_test(NAME direct_two COMMAND direct_two)
    """
        self.assertEqual(evolution_engine._count_ctest_targets(cmake), 4)

    def test_ignores_helper_definition_and_comments(self):
        cmake = """
    # ivanna_add_test(fake)
    function(ivanna_add_test name)
      # add_test(NAME commented COMMAND ignored)
      gtest_discover_tests(${name})
    endfunction()
    """
        self.assertEqual(evolution_engine._count_ctest_targets(cmake), 0)

    def test_deduplicates_repeated_names(self):
        cmake = """
    ivanna_add_test(foo)
    ivanna_add_test(foo)
    add_test(NAME foo COMMAND foo)
    """
        self.assertEqual(evolution_engine._count_ctest_targets(cmake), 1)


if __name__ == "__main__":
    unittest.main()
