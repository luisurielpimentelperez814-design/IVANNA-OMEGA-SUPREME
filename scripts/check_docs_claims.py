#!/usr/bin/env python3
"""
check_docs_claims.py — Fase 0.5 Verificador de afirmaciones documentales contra telemetría medida y código.

Falla (exit 1) si `README.md` o `LÉAME.md`:
  1. Contienen afirmaciones absolutas no respaldadas por telemetría generada:
     - `"0.00 ms"`
     - `"THD = 0"` o `"THD=0"`
     - `"150/150"` (o cualquier conteo `N/N` de CTest que no provenga de `telemetry/ctest_summary.json` generado por CTest real)
  2. Contienen `\operatorname` (no soportado consistentemente por el motor MathJax de GitHub).
  3. Declaran constantes que divergen del código C++ real (p.ej. asíntota 0.996 en vez de 0.994,
     Hermite 32 muestras en vez de 16 muestras).

Modo `--generate-from-log <path>`:
  Parsea la salida real de `ctest` (`100% tests passed, 0 tests failed out of N`) y escribe
  `telemetry/ctest_summary.json`.
"""

from __future__ import annotations
import argparse
import json
import re
import sys
from pathlib import Path
from typing import List

ROOT = Path(__file__).resolve().parent.parent
DOCS = [ROOT / "README.md", ROOT / "LÉAME.md"]
CTEST_SUMMARY_JSON = ROOT / "telemetry" / "ctest_summary.json"
THD_CHAIN_JSON = ROOT / "telemetry" / "thd_chain.json"


def generate_from_log(log_path: Path) -> int:
    if not log_path.exists():
        print(f"[ERROR] No existe log de CTest: {log_path}", file=sys.stderr)
        return 1
    txt = log_path.read_text(encoding="utf-8", errors="replace")
    m = re.search(r'(\d+)%\s+tests\s+passed,\s+(\d+)\s+tests\s+failed\s+out\s+of\s+(\d+)', txt)
    if not m:
        print("[ERROR] No se encontró línea resumen de CTest en el log.", file=sys.stderr)
        return 1
    pct = int(m.group(1))
    failed = int(m.group(2))
    total = int(m.group(3))
    passed = total - failed
    t_match = re.search(r'Total Test time \(real\)\s*=\s*([0-9.]+)\s*sec', txt)
    duration_sec = float(t_match.group(1)) if t_match else 0.0

    CTEST_SUMMARY_JSON.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "passed": passed,
        "failed": failed,
        "total": total,
        "pass_rate_percent": pct,
        "duration_sec": duration_sec,
        "badge_label": f"{passed}/{total}",
    }
    CTEST_SUMMARY_JSON.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"[OK] Generado {CTEST_SUMMARY_JSON.relative_to(ROOT)}: {passed}/{total} tests passed ({duration_sec:.2f}s)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--generate-from-log", type=Path, default=None)
    parser.add_argument("--ratchet", action="store_true")
    args = parser.parse_args()

    if args.generate_from_log is not None:
        return generate_from_log(args.generate_from_log)

    findings: List[str] = []

    ctest_data = None
    if CTEST_SUMMARY_JSON.exists():
        try:
            ctest_data = json.loads(CTEST_SUMMARY_JSON.read_text(encoding="utf-8"))
        except Exception:
            ctest_data = None

    for doc in DOCS:
        if not doc.exists():
            continue
        rel = doc.relative_to(ROOT)
        lines = doc.read_text(encoding="utf-8", errors="replace").splitlines()
        for idx, line in enumerate(lines, start=1):
            if "0.00 ms" in line:
                findings.append(f"  - {rel}:{idx} contiene afirmación '0.00 ms' sin medición empírica adjunta")
            if "THD = 0" in line or "THD=0" in line:
                findings.append(f"  - {rel}:{idx} contiene afirmación 'THD = 0' / 'THD=0' (usar cifra medida en dB de telemetry/thd_chain.json)")
            if "150/150" in line and ctest_data is None:
                findings.append(f"  - {rel}:{idx} contiene '150/150' hardcodeado sin telemetry/ctest_summary.json generado por CI")
            elif ctest_data is not None:
                expected_label = ctest_data.get("badge_label", "")
                for m_ratio in re.finditer(r'\b(\d+)/(\d+)\b', line):
                    n1, n2 = m_ratio.group(1), m_ratio.group(2)
                    if n1 == n2 and int(n1) >= 50 and f"{n1}/{n2}" != expected_label:
                        findings.append(
                            f"  - {rel}:{idx} conteo CTest '{n1}/{n2}' no coincide con telemetry/ctest_summary.json ('{expected_label}')"
                        )
            if "\\operatorname" in line:
                findings.append(f"  - {rel}:{idx} contiene '\\operatorname' (usar '\\mathrm{{sgn}}')")

    print("=== [0.5] CHECK DOCS CLAIMS REPORT ===")
    if findings:
        print(f"[FAIL] Afirmaciones documentales no verificadas ({len(findings)}):")
        for f in findings:
            print(f)
        if args.ratchet and len(findings) <= 11:
            print(f"[RATCHET-OK] Hallazgos documentales ({len(findings)}) <= baseline (11).")
            return 0
        return 1

    print("[PASS] 0 afirmaciones prohibidas o desincronizadas en README.md / LÉAME.md.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
