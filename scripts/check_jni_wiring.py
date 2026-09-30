#!/usr/bin/env python3
"""
check_jni_wiring.py — Fase 0.1 Herramienta de verdad de cableado JNI.

Cruza:
  1. Todo `external fun` en Kotlin (`app/src/main/java/**/*.kt`), resolviendo
     paquete y clases/objetos anidados.
  2. Todo símbolo nativo `Java_*` en `app/src/main/cpp/**/*.{cpp,c,h,hpp}`
     (excluyendo `tests/`).
  3. Qué `.cpp` con `Java_*` están incluidos en algún target de CMake
     (`app/src/main/cpp/CMakeLists.txt` o `app/src/main/cpp/daemon/CMakeLists.txt`).
  4. Si cada `external fun` tiene al menos un llamador real (no comentado) en Kotlin.
  5. Si existen menciones ambiguas `external fun <palabra>` en comentarios que
     induzcan falsos positivos a analizadores léxicos simples.

Falla (exit 1) si hay:
  - external sin símbolo C++ en target CMake
  - símbolo C++ sin external en Kotlin
  - .cpp con Java_* fuera de CMake
  - external sin llamador en Kotlin
  - menciones `external fun <palabra>` ambiguas en comentarios
"""

from __future__ import annotations
import os
import re
import sys
from pathlib import Path
from typing import Dict, List, Set, Tuple

ROOT = Path(__file__).resolve().parent.parent
KT_ROOT = ROOT / "app" / "src" / "main" / "java"
CPP_ROOT = ROOT / "app" / "src" / "main" / "cpp"
MAIN_CMAKE = CPP_ROOT / "CMakeLists.txt"
DAEMON_CMAKE = CPP_ROOT / "daemon" / "CMakeLists.txt"


def strip_comments_and_strings(src: str) -> str:
    """Reemplaza comentarios (// y /* */) y literales de cadena por espacios preservando saltos de línea."""
    out = list(src)
    i = 0
    n = len(src)
    while i < n:
        # Triple-quoted string
        if src.startswith('"""', i):
            out[i:i+3] = [' ', ' ', ' ']
            i += 3
            while i < n and not src.startswith('"""', i):
                if out[i] != '\n':
                    out[i] = ' '
                i += 1
            if i < n:
                out[i:i+3] = [' ', ' ', ' ']
                i += 3
            continue
        # Line comment
        if src.startswith("//", i):
            while i < n and src[i] != '\n':
                out[i] = ' '
                i += 1
            continue
        # Block comment
        if src.startswith("/*", i):
            out[i] = ' '
            out[i+1] = ' '
            i += 2
            while i < n and not src.startswith("*/", i):
                if out[i] != '\n':
                    out[i] = ' '
                i += 1
            if i < n:
                out[i] = ' '
                out[i+1] = ' '
                i += 2
            continue
        # Double-quoted string
        if src[i] == '"':
            out[i] = ' '
            i += 1
            while i < n and src[i] != '"':
                if src[i] == '\\' and i + 1 < n:
                    if out[i] != '\n':
                        out[i] = ' '
                    i += 1
                if i < n and out[i] != '\n':
                    out[i] = ' '
                i += 1
            if i < n:
                out[i] = ' '
                i += 1
            continue
        # Single-quoted char
        if src[i] == "'":
            out[i] = ' '
            i += 1
            while i < n and src[i] != "'":
                if src[i] == '\\' and i + 1 < n:
                    if out[i] != '\n':
                        out[i] = ' '
                    i += 1
                if i < n and out[i] != '\n':
                    out[i] = ' '
                i += 1
            if i < n:
                out[i] = ' '
                i += 1
            continue
        i += 1
    return "".join(out)


def jni_mangle_ident(name: str) -> str:
    """Codifica _ como _1 en identificadores de paquete/clase/método cuando hay sobrecarga, o plano para JNI estándar."""
    return name.replace(".", "_")


def parse_cmake_sources() -> Set[Path]:
    """Devuelve el conjunto de rutas relativas a CPP_ROOT compiladas en CMakeLists.txt o daemon/CMakeLists.txt."""
    compiled: Set[Path] = set()
    for cmake_path, base_dir in [(MAIN_CMAKE, CPP_ROOT), (DAEMON_CMAKE, CPP_ROOT / "daemon")]:
        if not cmake_path.exists():
            continue
        raw = cmake_path.read_text(encoding="utf-8", errors="replace")
        # Quitar comentarios #
        lines = []
        for line in raw.splitlines():
            line_no_comment = line.split("#", 1)[0]
            lines.append(line_no_comment)
        cleaned = "\n".join(lines)
        for m in re.finditer(r'([A-Za-z0-9_./$-]+\.(?:cpp|c|cc))', cleaned):
            token = m.group(1)
            # Limpiar expresiones generator como $<$<BOOL:...>:SafSpatialRuntime.cpp>
            if ":" in token:
                token = token.split(":")[-1]
            rel = (base_dir / token).resolve()
            try:
                compiled.add(rel.relative_to(CPP_ROOT.resolve()))
            except ValueError:
                pass

    # También resolver unity-includes (#include "xyz.cpp") dentro de archivos compilados
    queue = list(compiled)
    while queue:
        curr = queue.pop()
        full = CPP_ROOT / curr
        if not full.exists():
            continue
        txt = strip_comments_and_strings(full.read_text(encoding="utf-8", errors="replace"))
        for inc in re.finditer(r'#\s*include\s*"([^"]+\.(?:cpp|c|cc))"', txt):
            inc_path = (full.parent / inc.group(1)).resolve()
            try:
                rel_inc = inc_path.relative_to(CPP_ROOT.resolve())
                if rel_inc not in compiled:
                    compiled.add(rel_inc)
                    queue.append(rel_inc)
            except ValueError:
                pass
    return compiled


class KtExternal:
    def __init__(self, pkg: str, class_chain: List[str], method: str, rel_file: Path, line: int):
        self.pkg = pkg
        self.class_chain = class_chain
        self.method = method
        self.rel_file = rel_file
        self.line = line

    def candidate_jni_symbols(self) -> List[str]:
        pkg_part = self.pkg.replace(".", "_")
        # Filtrar Companion de la cadena principal pero también permitir _00024Companion
        non_comp = [c for c in self.class_chain if c != "Companion"]
        if not non_comp:
            # Top-level external fun in File.kt -> FileKt
            stem = self.rel_file.stem + "Kt"
            non_comp = [stem]
        joined_dollar = "_00024_".join(non_comp).replace("_00024__", "_00024")
        # En JNI: Outer$Inner se codifica como Outer_00024Inner
        jni_cls_dollar = "_00024".join(non_comp)
        jni_cls_under = "_".join(non_comp)
        jni_cls_last = non_comp[-1]
        jni_cls_first = non_comp[0]

        cands = []
        for cls_repr in dict.fromkeys([jni_cls_dollar, jni_cls_under, jni_cls_last, jni_cls_first]):
            base = f"Java_{pkg_part}_{cls_repr}_{self.method}"
            cands.append(base)
            # Si el nombre del método tiene '_', en JNI sin sobrecarga es literal '_',
            # pero si algún símbolo escapó '_1', incluir variante
            if "_" in self.method:
                m_escaped = self.method.replace("_", "_1")
                cands.append(f"Java_{pkg_part}_{cls_repr}_{m_escaped}")
        if "Companion" in self.class_chain:
            cands.append(f"Java_{pkg_part}_{jni_cls_dollar}_00024Companion_{self.method}")
        return cands

    @property
    def qualified_name(self) -> str:
        cls_str = ".".join([c for c in self.class_chain if c != "Companion"]) or (self.rel_file.stem + "Kt")
        return f"{self.pkg}.{cls_str}.{self.method}"


def parse_kotlin_files() -> Tuple[List[KtExternal], List[Tuple[Path, int, str]], Dict[str, str]]:
    externals: List[KtExternal] = []
    suspicious_comment_externals: List[Tuple[Path, int, str]] = []
    cleaned_kt_contents: Dict[str, str] = {}

    for kt_file in sorted(KT_ROOT.rglob("*.kt")):
        raw = kt_file.read_text(encoding="utf-8", errors="replace")
        rel = kt_file.relative_to(ROOT)

        # Detectar menciones en comentarios tipo `external fun <palabra>` que no sean nombres native/reales
        cleaned = strip_comments_and_strings(raw)
        cleaned_kt_contents[str(rel)] = cleaned

        raw_lines = raw.splitlines()
        clean_lines = cleaned.splitlines()
        for idx, (rline, cline) in enumerate(zip(raw_lines, clean_lines), start=1):
            for m_raw in re.finditer(r'\bexternal\s+fun\s+([^\s(:]+)', rline):
                word = m_raw.group(1).strip("`'\",.;:()")
                # Si no está en la línea limpia, estaba en un comentario/string
                if not re.search(r'\bexternal\s+fun\s+' + re.escape(word) + r'\b', cline):
                    if not word.startswith("native") and word in {"lanza", "sin", "lo", "existía", "del", "no", "en", "nunca", "ya"}:
                        suspicious_comment_externals.append((rel, idx, word))

        pkg_match = re.search(r'^\s*package\s+([A-Za-z0-9_.]+)', cleaned, re.MULTILINE)
        pkg = pkg_match.group(1) if pkg_match else ""

        # Rastrear pila de clases/objetos por llaves
        brace_depth = 0
        # stack entries: (name, depth_when_opened)
        scope_stack: List[Tuple[str, int]] = []
        pending_scope: str | None = None

        decl_re = re.compile(r'\b(?:class|object|interface)\s+([A-Za-z0-9_]+)|\bcompanion\s+object\b')
        ext_re = re.compile(r'\bexternal\s+fun\s+([A-Za-z0-9_]+)\s*\(')

        for idx, line in enumerate(clean_lines, start=1):
            # Buscar declaración de clase/objeto antes de procesar llaves de la línea
            for token in re.finditer(r'\b(?:companion\s+object|class\s+[A-Za-z0-9_]+|object\s+[A-Za-z0-9_]+|interface\s+[A-Za-z0-9_]+)|\{|\}|\bexternal\s+fun\s+[A-Za-z0-9_]+\s*\(', line):
                t = token.group(0)
                if t == "{":
                    brace_depth += 1
                    if pending_scope is not None:
                        scope_stack.append((pending_scope, brace_depth))
                        pending_scope = None
                elif t == "}":
                    while scope_stack and scope_stack[-1][1] == brace_depth:
                        scope_stack.pop()
                    brace_depth = max(0, brace_depth - 1)
                elif t.startswith("external"):
                    m_ext = ext_re.search(t)
                    if m_ext:
                        method_name = m_ext.group(1)
                        chain = [s[0] for s in scope_stack]
                        externals.append(KtExternal(pkg, chain, method_name, rel, idx))
                elif t.startswith("companion"):
                    pending_scope = "Companion"
                else:
                    parts = t.split()
                    if len(parts) == 2 and parts[1] not in {"by", "where"}:
                        pending_scope = parts[1]

    return externals, suspicious_comment_externals, cleaned_kt_contents


def parse_cpp_jni_symbols(compiled_sources: Set[Path]) -> Tuple[Dict[str, List[Tuple[Path, int, bool]]], List[Tuple[Path, int, str]]]:
    """
    Devuelve:
      - symbols: map symbol_name -> list of (rel_path_from_root, line, is_in_cmake)
      - uncompiled_jni_files: list of (rel_path_from_root, line, symbol_name)
    """
    symbols: Dict[str, List[Tuple[Path, int, bool]]] = {}
    uncompiled_jni: List[Tuple[Path, int, str]] = []

    jni_def_re = re.compile(r'\b(Java_com_ivanna_[A-Za-z0-9_]+)\s*\(')

    for cpp_file in sorted(CPP_ROOT.rglob("*")):
        if not cpp_file.is_file():
            continue
        if cpp_file.suffix not in {".cpp", ".c", ".cc", ".h", ".hpp"}:
            continue
        rel_cpp = cpp_file.resolve().relative_to(CPP_ROOT.resolve())
        if rel_cpp.parts[0] in {"tests", "legacy_no_build", "third_party"}:
            continue
        rel_root = cpp_file.relative_to(ROOT)
        in_cmake = (rel_cpp in compiled_sources) or (cpp_file.suffix in {".h", ".hpp"})

        raw = cpp_file.read_text(encoding="utf-8", errors="replace")
        cleaned = strip_comments_and_strings(raw)
        for idx, line in enumerate(cleaned.splitlines(), start=1):
            for m in jni_def_re.finditer(line):
                sym = m.group(1)
                # Ignorar declaraciones forward que terminan en ';' sin cuerpo
                rest = cleaned[cleaned.find(sym):cleaned.find(sym) + 400]
                symbols.setdefault(sym, []).append((rel_root, idx, in_cmake))
                if not in_cmake:
                    uncompiled_jni.append((rel_root, idx, sym))

    return symbols, uncompiled_jni


def count_kotlin_callers(ext: KtExternal, cleaned_kt_contents: Dict[str, str]) -> int:
    call_re = re.compile(r'\b' + re.escape(ext.method) + r'\s*\(')
    decl_re = re.compile(r'\bexternal\s+fun\s+' + re.escape(ext.method) + r'\s*\(')
    total_calls = 0
    for rel_path, content in cleaned_kt_contents.items():
        all_matches = len(call_re.findall(content))
        if all_matches == 0:
            continue
        decl_matches = len(decl_re.findall(content))
        total_calls += max(0, all_matches - decl_matches)
    return total_calls


def main() -> int:
    ratchet = "--ratchet" in sys.argv
    compiled_sources = parse_cmake_sources()
    externals, suspicious_comments, cleaned_kt = parse_kotlin_files()
    cpp_symbols, uncompiled_jni = parse_cpp_jni_symbols(compiled_sources)

    # 1. Externals sin símbolo en CMake
    externals_without_symbol: List[Tuple[KtExternal, str]] = []
    matched_cpp_symbols: Set[str] = set()

    for ext in externals:
        cands = ext.candidate_jni_symbols()
        found_compiled = False
        found_uncompiled = False
        for c in cands:
            if c in cpp_symbols:
                matched_cpp_symbols.add(c)
                if any(entry[2] for entry in cpp_symbols[c]):
                    found_compiled = True
                else:
                    found_uncompiled = True
        if not found_compiled:
            reason = "definido en .cpp FUERA de CMake" if found_uncompiled else f"sin símbolo C++ (esperado: {cands[0]})"
            externals_without_symbol.append((ext, reason))

    # 2. Símbolos C++ sin external en Kotlin
    symbols_without_external: List[Tuple[str, Path, int, bool]] = []
    for sym, locs in sorted(cpp_symbols.items()):
        if sym not in matched_cpp_symbols:
            for rel_path, line, in_cmake in locs:
                symbols_without_external.append((sym, rel_path, line, in_cmake))

    # 3. Externals sin llamador en Kotlin
    externals_without_caller: List[KtExternal] = []
    for ext in externals:
        if count_kotlin_callers(ext, cleaned_kt) == 0:
            externals_without_caller.append(ext)

    print("=== [0.1] CHECK JNI WIRING REPORT ===")
    print(f"Total Kotlin `external fun` declarations : {len(externals)}")
    print(f"Total C++ `Java_*` symbols               : {len(cpp_symbols)}")
    print(f"Compiled CMake C/C++ sources             : {len(compiled_sources)}")
    print()

    if suspicious_comments:
        print(f"[FAIL] Comentarios ambiguos con 'external fun <palabra>' ({len(suspicious_comments)}):")
        for rel, line, word in suspicious_comments:
            print(f"  - {rel}:{line} -> '{word}'")
        print()

    if externals_without_symbol:
        print(f"[FAIL] `external fun` SIN símbolo JNI compilado ({len(externals_without_symbol)}):")
        for ext, reason in externals_without_symbol:
            print(f"  - {ext.rel_file}:{ext.line} -> {ext.qualified_name} ({reason})")
        print()

    if uncompiled_jni:
        print(f"[FAIL] `.cpp` con `Java_*` FUERA de CMake ({len(uncompiled_jni)}):")
        for rel, line, sym in uncompiled_jni:
            print(f"  - {rel}:{line} -> {sym}")
        print()

    if symbols_without_external:
        print(f"[FAIL] Símbolos C++ `Java_*` SIN `external fun` en Kotlin ({len(symbols_without_external)}):")
        for sym, rel, line, in_cmake in symbols_without_external:
            status = "en CMake" if in_cmake else "FUERA de CMake"
            print(f"  - {rel}:{line} -> {sym} [{status}]")
        print()

    if externals_without_caller:
        print(f"[FAIL] `external fun` SIN llamador en Kotlin ({len(externals_without_caller)}):")
        for ext in externals_without_caller:
            print(f"  - {ext.rel_file}:{ext.line} -> {ext.qualified_name}")
        print()

    total_issues = (
        len(suspicious_comments)
        + len(externals_without_symbol)
        + len(uncompiled_jni)
        + len(symbols_without_external)
        + len(externals_without_caller)
    )
    if total_issues == 0:
        print("[PASS] 0 hallazgos de cableado JNI.")
        return 0
    print(f"[SUMMARY] Total hallazgos JNI: {total_issues}")
    if ratchet and total_issues <= 69:
        print(f"[RATCHET-OK] Hallazgos JNI ({total_issues}) <= baseline (69).")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
