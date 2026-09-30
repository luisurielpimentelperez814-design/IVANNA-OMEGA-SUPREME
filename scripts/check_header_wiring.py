#!/usr/bin/env python3
"""
check_header_wiring.py — Fase 0.2 Herramienta de verdad de headers C/C++ en producción.

Verifica que todo archivo `.h` / `.hpp` de producción bajo `app/src/main/cpp/`
(excluyendo `tests/`) sea alcanzado transitivamente por `#include` desde al menos
una unidad de traducción (`.cpp` / `.c`) compilada en `CMakeLists.txt` o
`daemon/CMakeLists.txt`.

Falla (exit 1) si algún header de producción está huérfano.
"""

from __future__ import annotations
import re
import sys
from pathlib import Path
from typing import List, Set

ROOT = Path(__file__).resolve().parent.parent
CPP_ROOT = ROOT / "app" / "src" / "main" / "cpp"
MAIN_CMAKE = CPP_ROOT / "CMakeLists.txt"
DAEMON_CMAKE = CPP_ROOT / "daemon" / "CMakeLists.txt"

INCLUDE_DIRS = [
    CPP_ROOT,
    CPP_ROOT / "include",
    CPP_ROOT / "neuromorphic",
    CPP_ROOT / "spatial",
    CPP_ROOT / "supreme",
    CPP_ROOT / "visualizer",
    CPP_ROOT / "experimental" / "adaptive_engine",
    CPP_ROOT / "ivannalab",
    CPP_ROOT / "daemon",
    CPP_ROOT / "daemon" / "core",
    CPP_ROOT / "daemon" / "control",
    CPP_ROOT / "music_intelligence",
    CPP_ROOT / "ime",
    CPP_ROOT / "hexagon",
]


def strip_comments(src: str) -> str:
    src = re.sub(r'/\*.*?\*/', ' ', src, flags=re.DOTALL)
    src = re.sub(r'//[^\n]*', ' ', src)
    return src


def get_compiled_sources() -> Set[Path]:
    compiled: Set[Path] = set()
    for cmake_path, base_dir in [(MAIN_CMAKE, CPP_ROOT), (DAEMON_CMAKE, CPP_ROOT / "daemon")]:
        if not cmake_path.exists():
            continue
        raw = cmake_path.read_text(encoding="utf-8", errors="replace")
        cleaned = "\n".join(line.split("#", 1)[0] for line in raw.splitlines())
        for m in re.finditer(r'([A-Za-z0-9_./$-]+\.(?:cpp|c|cc))', cleaned):
            token = m.group(1)
            if ":" in token:
                token = token.split(":")[-1]
            full = (base_dir / token).resolve()
            if full.exists():
                try:
                    rel = full.relative_to(CPP_ROOT.resolve())
                    if rel.parts[0] != "tests":
                        compiled.add(full)
                except ValueError:
                    pass
    return compiled


def resolve_include(inc_str: str, parent_file: Path) -> Path | None:
    cand_local = (parent_file.parent / inc_str).resolve()
    if cand_local.exists() and cand_local.is_file():
        return cand_local
    for d in INCLUDE_DIRS:
        cand = (d / inc_str).resolve()
        if cand.exists() and cand.is_file():
            return cand
    return None


def main() -> int:
    all_headers: Set[Path] = set()
    for p in sorted(CPP_ROOT.rglob("*")):
        if not p.is_file() or p.suffix not in {".h", ".hpp"}:
            continue
        rel = p.resolve().relative_to(CPP_ROOT.resolve())
        if rel.parts[0] in {"tests", "legacy_no_build"}:
            continue
        all_headers.add(p.resolve())

    compiled_tus = get_compiled_sources()
    visited_files: Set[Path] = set(compiled_tus)
    reached_headers: Set[Path] = set()

    queue: List[Path] = list(compiled_tus)
    inc_re = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)

    while queue:
        curr = queue.pop()
        if not curr.exists():
            continue
        txt = strip_comments(curr.read_text(encoding="utf-8", errors="replace"))
        for m in inc_re.finditer(txt):
            inc_target = m.group(1)
            resolved = resolve_include(inc_target, curr)
            if resolved is None:
                continue
            try:
                rel = resolved.relative_to(CPP_ROOT.resolve())
            except ValueError:
                continue
            if rel.parts[0] == "tests":
                continue
            if resolved.suffix in {".h", ".hpp"}:
                reached_headers.add(resolved)
            if resolved not in visited_files:
                visited_files.add(resolved)
                queue.append(resolved)

    orphans = sorted(all_headers - reached_headers)

    print("=== [0.2] CHECK HEADER WIRING REPORT ===")
    print(f"Total production headers (.h/.hpp) : {len(all_headers)}")
    print(f"Reached from compiled CMake TUs    : {len(reached_headers)}")
    print(f"Orphan headers                     : {len(orphans)}")
    print()

    if orphans:
        print(f"[FAIL] Headers de producción huérfanos ({len(orphans)}):")
        for h in orphans:
            print(f"  - {h.relative_to(ROOT)}")
        if "--ratchet" in sys.argv and len(orphans) <= 22:
            print(f"[RATCHET-OK] Headers huérfanos ({len(orphans)}) <= baseline (22).")
            return 0
        return 1

    print("[PASS] 0 headers de producción huérfanos.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
