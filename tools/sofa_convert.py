#!/usr/bin/env python3
"""sofa_convert.py — SHIM DE COMPATIBILIDAD. No convierte nada por si mismo.

POR QUE ESTE ARCHIVO YA NO ESCRIBE BINARIOS (auditoria 2026-09-09, flanco
herramientas HRTF — todo verificado por lectura directa):

  La implementacion original era una variante muerta y peligrosa de la
  herramienta canonica tools/hrtf/sofa_to_ihr1.py:

  1. SIN CLI: rutas hardcodeadas (MIT_KEMAR_normal_pinna.sofa ->
     hrtf_database.bin). Ejecutarla pisaba el asset distribuido sin aviso.
  2. SIN RESAMPLEO: escribia los HRIR al sample rate del SOFA (44100 en los
     KEMAR) pero el cargador C++ (synthetic_hrtf.hpp) IGNORA el campo SR de
     la cabecera y reproduce los taps al SR del motor (48000). Resultado
     audible: ITD encogido un 8.8% y notches espectrales desplazados —
     las pistas de elevacion suenan donde no deben.
  3. SIN AZIMUTS: escribia el formato legacy IVHRTF01 (sin tabla angular),
     que el motor NO prioriza — el camino vivo es IHR1 con az/el.
  4. Formato fragil demostrado en produccion: el asset hrtf_database.bin
     estuvo CORRUPTO en main entre bde62755 y 73812665 (pasado por un codec
     de texto UTF-8 — cabecera con 45.9M posiciones x 33.5M taps, OOM
     garantizado si el motor lo cargaba). Restaurado byte-perfecto desde
     996a6259. Nadie debe regenerarlo con herramientas ad-hoc.

  Arreglar este script equivaldria a clonar sofa_to_ihr1.py — duplicar la
  herramienta buena es exactamente el tipo de divergencia que causo el
  desastre de arriba. En su lugar: redirige a la canonica y sale con
  codigo de error si falta el argumento.

USO CORRECTO (la herramienta canonica hace resampleo polifasico, dedupe
por azimut con |elevacion| minima, convencion de signo SOFA->convolver y
verifica los limites del cargador ANTES de escribir):

  python3 tools/hrtf/sofa_to_ihr1.py <archivo.sofa> -o <salida.ihr1>

  # Ejemplo con el KEMAR del repo, tal como lo quiere el motor (48 kHz):
  python3 tools/hrtf/sofa_to_ihr1.py \\
      app/src/main/assets/saf/sofa_elite/MIT_KEMAR_normal_pinna.sofa \\
      -o hrtf_dataset.ihr1

  # Validar el resultado contra el formato que el lector C++ acepta:
  python3 tools/hrtf/verify_dataset.py hrtf_dataset.ihr1
"""

import os
import subprocess
import sys

CANONICAL = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "hrtf", "sofa_to_ihr1.py"
)


def main() -> int:
    print(
        "sofa_convert.py ya no convierte: era una variante sin CLI, sin\n"
        "resampleo y sin azimuts (formato legacy que el motor no prioriza).\n"
        "Redirigiendo a la herramienta canonica: tools/hrtf/sofa_to_ihr1.py\n",
        file=sys.stderr,
    )
    if not os.path.isfile(CANONICAL):
        print(f"ERROR: no se encuentra {CANONICAL}", file=sys.stderr)
        return 2
    if len(sys.argv) < 2:
        # Sin argumentos: mostrar la ayuda de la canonica y salir con error
        # para que ningun script/CI heredado crea que genero algo.
        subprocess.run([sys.executable, CANONICAL, "--help"], check=False)
        return 2
    # Con argumentos: delegar tal cual, conservando el codigo de salida.
    return subprocess.run([sys.executable, CANONICAL, *sys.argv[1:]], check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
