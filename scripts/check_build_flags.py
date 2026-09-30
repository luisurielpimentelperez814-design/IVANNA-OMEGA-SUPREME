#!/usr/bin/env python3
"""
check_build_flags.py — Fase 0.4 Verificador de flags IEEE-754 seguros en CMake y Gradle.

Falla (exit 1) si algún archivo `CMakeLists.txt`, `*.cmake`, `*.gradle` o `*.gradle.kts`
contiene fuera de comentarios:
  - `-ffast-math`
  - `-Ofast`
  - `-ffinite-math-only`
"""

from __future__ import annotations
import re
import sys
from pathlib import Path
from typing import List

ROOT = Path(__file__).resolve().parent.parent

FORBIDDEN_FLAGS = [
    "-ffast-math",
    "-Ofast",
    "-ffinite-math-only",
]


def strip_comments_cmake(src: str) -> str:
    return "\n".join(line.split("#", 1)[0] for line in src.splitlines())


def strip_comments_gradle(src: str) -> str:
    src = re.sub(r'/\*.*?\*/', lambda m: "\n" * m.group(0).count("\n"), src, flags=re.DOTALL)
    return "\n".join(line.split("//", 1)[0] for line in src.splitlines())


def main() -> int:
    target_files: List[Path] = []
    for p in sorted(ROOT.rglob("*")):
        if not p.is_file():
            continue
        rel_parts = p.relative_to(ROOT).parts
        if any(part in {".git", "node_modules", "build", "third_party"} for part in rel_parts):
            continue
        if p.name == "CMakeLists.txt" or p.suffix in {".cmake", ".gradle"} or p.name.endswith(".gradle.kts"):
            target_files.append(p)

    findings: List[str] = []
    for f in target_files:
        raw = f.read_text(encoding="utf-8", errors="replace")
        cleaned = strip_comments_cmake(raw) if (f.name == "CMakeLists.txt" or f.suffix == ".cmake") else strip_comments_gradle(raw)
        for line_no, line in enumerate(cleaned.splitlines(), start=1):
            for flag in FORBIDDEN_FLAGS:
                if re.search(r'(?<!\S)' + re.escape(flag) + r'(?!\S)', line):
                    findings.append(f"  - {f.relative_to(ROOT)}:{line_no} contiene `{flag}`: `{line.strip()}`")

    print("=== [0.4] CHECK BUILD FLAGS REPORT ===")
    print(f"Archivos CMake/Gradle auditados: {len(target_files)}")
    if findings:
        print(f"[FAIL] Flags peligrosos detectados ({len(findings)}):")
        for item in findings:
            print(item)
        return 1

    print("[PASS] 0 flags prohibidos (-ffast-math / -Ofast / -ffinite-math-only).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
