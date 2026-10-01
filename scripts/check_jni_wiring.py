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
    def __init__(
        self,
        pkg: str,
        class_chain: List[str],
        method: str,
        rel_file: Path,
        line: int,
        class_span: Tuple[int, int] = (0, 0),
    ):
        self.pkg = pkg
        self.class_chain = class_chain
        self.method = method
        self.rel_file = rel_file
        self.line = line
        self.class_span = class_span

    @property
    def owner_class(self) -> str:
        non_comp = [c for c in self.class_chain if c != "Companion"]
        return non_comp[-1] if non_comp else (self.rel_file.stem + "Kt")

    def candidate_jni_symbols(self) -> List[str]:
        pkg_part = self.pkg.replace(".", "_")
        # Filtrar Companion de la cadena principal pero también permitir _00024Companion
        non_comp = [c for c in self.class_chain if c != "Companion"]
        if not non_comp:
            # Top-level external fun in File.kt -> FileKt
            stem = self.rel_file.stem + "Kt"
            non_comp = [stem]
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


def extract_kotlin_externals_from_source(
    raw: str, rel: Path
) -> Tuple[List[KtExternal], List[Tuple[Path, int, str]], str]:
    externals: List[KtExternal] = []
    suspicious_comment_externals: List[Tuple[Path, int, str]] = []

    cleaned = strip_comments_and_strings(raw)
    raw_lines = raw.splitlines()
    clean_lines = cleaned.splitlines()

    for idx, (rline, cline) in enumerate(zip(raw_lines, clean_lines), start=1):
        for m_raw in re.finditer(r'\bexternal\s+fun\s+([^\s(:]+)', rline):
            word = m_raw.group(1).strip("`'\",.;:()")
            if not re.search(r'\bexternal\s+fun\s+' + re.escape(word) + r'\b', cline):
                if not word.startswith("native") and word in {
                    "lanza", "sin", "lo", "existía", "del", "no", "en", "nunca", "ya"
                }:
                    suspicious_comment_externals.append((rel, idx, word))

    pkg_match = re.search(r'^\s*package\s+([A-Za-z0-9_.]+)', cleaned, re.MULTILINE)
    pkg = pkg_match.group(1) if pkg_match else ""

    brace_depth = 0
    # stack entries: [name, depth_when_opened, start_line, ext_indices_inside]
    scope_stack: List[List[object]] = []
    pending_scope: Tuple[str, int] | None = None
    ext_re = re.compile(r'\bexternal\s+fun\s+([A-Za-z0-9_]+)\s*\(')
    token_re = re.compile(
        r'\b(?:companion\s+object|class\s+[A-Za-z0-9_]+|object\s+[A-Za-z0-9_]+|interface\s+[A-Za-z0-9_]+)'
        r'|\{|\}|\bexternal\s+fun\s+[A-Za-z0-9_]+\s*\('
    )

    for idx, line in enumerate(clean_lines, start=1):
        for token in token_re.finditer(line):
            t = token.group(0)
            if t == "{":
                brace_depth += 1
                if pending_scope is not None:
                    scope_stack.append([pending_scope[0], brace_depth, pending_scope[1], []])
                    pending_scope = None
            elif t == "}":
                while scope_stack and scope_stack[-1][1] == brace_depth:
                    popped = scope_stack.pop()
                    s_name, _, s_start, ext_idxs = popped
                    if s_name != "Companion":
                        for e_idx in ext_idxs:
                            if externals[e_idx].class_span == (0, 0):
                                externals[e_idx].class_span = (int(s_start), idx)
                brace_depth = max(0, brace_depth - 1)
            elif t.startswith("external"):
                m_ext = ext_re.search(t)
                if m_ext:
                    method_name = m_ext.group(1)
                    chain = [str(s[0]) for s in scope_stack]
                    ext_idx = len(externals)
                    externals.append(KtExternal(pkg, chain, method_name, rel, idx))
                    for s in scope_stack:
                        if s[0] != "Companion":
                            s[3].append(ext_idx)
            elif t.startswith("companion"):
                pending_scope = ("Companion", idx)
            else:
                parts = t.split()
                if len(parts) == 2 and parts[1] not in {"by", "where"}:
                    pending_scope = (parts[1], idx)

    total_lines = len(clean_lines)
    while scope_stack:
        popped = scope_stack.pop()
        s_name, _, s_start, ext_idxs = popped
        if s_name != "Companion":
            for e_idx in ext_idxs:
                if externals[e_idx].class_span == (0, 0):
                    externals[e_idx].class_span = (int(s_start), total_lines)

    for ext in externals:
        if ext.class_span == (0, 0):
            ext.class_span = (1, total_lines)

    return externals, suspicious_comment_externals, cleaned


def parse_kotlin_files() -> Tuple[List[KtExternal], List[Tuple[Path, int, str]], Dict[str, str]]:
    externals: List[KtExternal] = []
    suspicious_comment_externals: List[Tuple[Path, int, str]] = []
    cleaned_kt_contents: Dict[str, str] = {}

    for kt_file in sorted(KT_ROOT.rglob("*.kt")):
        raw = kt_file.read_text(encoding="utf-8", errors="replace")
        rel = kt_file.relative_to(ROOT)
        f_exts, f_susp, cleaned = extract_kotlin_externals_from_source(raw, rel)
        externals.extend(f_exts)
        suspicious_comment_externals.extend(f_susp)
        cleaned_kt_contents[str(rel)] = cleaned

    return externals, suspicious_comment_externals, cleaned_kt_contents


def infer_kotlin_var_types(cleaned_kt_contents: Dict[str, str]) -> Tuple[Dict[str, Dict[str, Set[str]]], Dict[str, Set[str]]]:
    """
    Infiere tipos de variables/propiedades por archivo y globalmente:
      - val/var x: Clase
      - val/var x = Clase(...) / Clase.shared / Clase.getInstance(...)
      - param x: Clase
      - remember { Clase(...) } / by lazy { Clase(...) }
    """
    per_file: Dict[str, Dict[str, Set[str]]] = {}
    global_props: Dict[str, Set[str]] = {}

    pat_explicit = re.compile(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z_][A-Za-z0-9_.]*)\s*\??')
    pat_ctor = re.compile(
        r'\b(?:val|var)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?::\s*[A-Za-z0-9_.?]+\s*)?=\s*'
        r'(?:remember\s*(?:\([^)]*\))?\s*\{\s*)?([A-Z][A-Za-z0-9_]*)\s*(?:\(|\.\s*(?:shared|instance|getInstance)\b)'
    )
    pat_lazy = re.compile(
        r'\b(?:val|var)\s+([A-Za-z_][A-Za-z0-9_]*)\s+by\s+(?:lazy\s*\{\s*([A-Z][A-Za-z0-9_]*)\s*\(|(?:activity)?[vV]iewModels\s*<\s*([A-Z][A-Za-z0-9_]*)\s*>)'
    )
    pat_vm = re.compile(
        r'\b(?:val|var)\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*viewModel\s*<\s*([A-Z][A-Za-z0-9_]*)\s*>'
    )

    keywords = {"if", "for", "while", "when", "return", "class", "object", "interface", "fun", "val", "var"}

    for rel_path, content in cleaned_kt_contents.items():
        fmap: Dict[str, Set[str]] = {}
        for m in pat_explicit.finditer(content):
            var_name, type_raw = m.group(1), m.group(2)
            if var_name in keywords:
                continue
            simple_type = type_raw.split(".")[-1]
            if simple_type and simple_type[0].isupper():
                fmap.setdefault(var_name, set()).add(simple_type)
                global_props.setdefault(var_name, set()).add(simple_type)
        for m in pat_ctor.finditer(content):
            var_name, cls_name = m.group(1), m.group(2)
            fmap.setdefault(var_name, set()).add(cls_name)
            global_props.setdefault(var_name, set()).add(cls_name)
        for m in pat_lazy.finditer(content):
            var_name = m.group(1)
            cls_name = m.group(2) or m.group(3)
            if cls_name:
                fmap.setdefault(var_name, set()).add(cls_name)
                global_props.setdefault(var_name, set()).add(cls_name)
        for m in pat_vm.finditer(content):
            var_name, cls_name = m.group(1), m.group(2)
            fmap.setdefault(var_name, set()).add(cls_name)
            global_props.setdefault(var_name, set()).add(cls_name)
        per_file[rel_path] = fmap

    return per_file, global_props


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
                symbols.setdefault(sym, []).append((rel_root, idx, in_cmake))
                if not in_cmake:
                    uncompiled_jni.append((rel_root, idx, sym))

    return symbols, uncompiled_jni


def count_kotlin_callers(
    ext: KtExternal,
    cleaned_kt_contents: Dict[str, str],
    per_file_types: Dict[str, Dict[str, Set[str]]] | None = None,
    global_types: Dict[str, Set[str]] | None = None,
) -> int:
    """
    Resuelve llamadores por CLASE contenedora de cada `external fun`:
      1. Llamada cualificada por clase/objeto: `Clase.fn(` o `Clase.Companion.fn(`
      2. Llamada cualificada por receptor con tipo conocido `objeto.fn(` donde `objeto: Clase`
      3. Llamada dentro del cuerpo de la propia clase contenedora (`fn(`, `this.fn(`, `Companion.fn(`)
         excluyendo la declaración `external fun fn(` y cualquier cabecera `fun fn(`.
    """
    if per_file_types is None or global_types is None:
        per_file_types, global_types = infer_kotlin_var_types(cleaned_kt_contents)

    cls = ext.owner_class
    method = ext.method
    ext_file_key = str(ext.rel_file)
    span_start, span_end = ext.class_span

    # Patrones precompilados
    decl_fun_re = re.compile(r'\bfun\s+' + re.escape(method) + r'\s*\(')
    qual_cls_re = re.compile(
        r'\b' + re.escape(cls) + r'\s*(?:\.\s*Companion\s*)?(?:\?\.|\.)\s*' + re.escape(method) + r'\s*\('
    )
    qual_recv_re = re.compile(
        r'\b([A-Za-z_][A-Za-z0-9_]*)\s*(?:\?\.|\.)\s*' + re.escape(method) + r'\s*\('
    )
    unqual_or_self_re = re.compile(
        r'(?:(?<![.\w?])|(?:this|Companion)\s*\.\s*)' + re.escape(method) + r'\s*\('
    )

    total_calls = 0
    for rel_path, content in cleaned_kt_contents.items():
        if method not in content:
            continue
        f_types = per_file_types.get(rel_path, {})
        lines = content.splitlines()
        for line_no, line in enumerate(lines, start=1):
            if method not in line:
                continue
            # Excluir la propia línea de declaración external y cualquier declaración `fun <method>(`
            if rel_path == ext_file_key and line_no == ext.line:
                continue
            line_wo_decl = decl_fun_re.sub(" ", line)

            # 1) Llamada cualificada `Clase.method(`
            cls_matches = len(qual_cls_re.findall(line_wo_decl))
            if cls_matches > 0:
                total_calls += cls_matches

            # 2) Llamada por receptor `obj.method(` con tipo conocido == cls
            for m_recv in qual_recv_re.finditer(line_wo_decl):
                recv = m_recv.group(1)
                if recv in {cls, "Companion", "this"}:
                    continue
                known_types = f_types.get(recv) or global_types.get(recv, set())
                if cls in known_types:
                    total_calls += 1

            # 3) Llamada dentro de la propia clase contenedora (no cualificada o this/Companion)
            if rel_path == ext_file_key and span_start <= line_no <= span_end:
                # Quitar matches ya contados de Clase.method(
                line_for_self = qual_cls_re.sub(" ", line_wo_decl)
                self_matches = len(unqual_or_self_re.findall(line_for_self))
                total_calls += self_matches

    return total_calls


def run_self_test() -> None:
    """
    Caso de prueba (A1): dos clases distintas que declaran el mismo nombre de método
    `nativeProcessBlock`. Solo `EngineAlpha.nativeProcessBlock` es invocado;
    `EngineBeta.nativeProcessBlock` NO tiene llamador y debe reportar 0 llamadores.
    """
    synthetic_src = """
    package com.ivanna.omega.test

    object EngineAlpha {
        external fun nativeProcessBlock(ptr: Long, frames: Int): Int
        fun process(ptr: Long) {
            nativeProcessBlock(ptr, 512)
        }
    }

    class EngineBeta {
        external fun nativeProcessBlock(ptr: Long, frames: Int): Int
    }

    class Consumer(private val alpha: EngineAlpha) {
        fun tick() {
            EngineAlpha.nativeProcessBlock(1L, 256)
            alpha.nativeProcessBlock(2L, 256)
        }
    }
    """
    fake_path = Path("app/src/main/java/com/ivanna/omega/test/SyntheticCollision.kt")
    exts, _, cleaned = extract_kotlin_externals_from_source(synthetic_src, fake_path)
    assert len(exts) == 2, f"Se esperaban 2 externals en self-test, obtenidos {len(exts)}"
    ext_alpha = next(e for e in exts if e.owner_class == "EngineAlpha")
    ext_beta = next(e for e in exts if e.owner_class == "EngineBeta")

    kt_map = {str(fake_path): cleaned}
    f_types, g_types = infer_kotlin_var_types(kt_map)
    calls_alpha = count_kotlin_callers(ext_alpha, kt_map, f_types, g_types)
    calls_beta = count_kotlin_callers(ext_beta, kt_map, f_types, g_types)

    if calls_alpha < 2:
        raise AssertionError(f"Self-test A1 falló: EngineAlpha.nativeProcessBlock esperaba >=2 llamadas, obtuvo {calls_alpha}")
    if calls_beta != 0:
        raise AssertionError(f"Self-test A1 falló: EngineBeta.nativeProcessBlock esperaba 0 llamadas, obtuvo {calls_beta}")


def main() -> int:
    run_self_test()
    if "--self-test" in sys.argv:
        print("[PASS] Self-test A1 (colisión de nombres entre dos clases) verificado.")
        return 0
    ratchet = "--ratchet" in sys.argv
    compiled_sources = parse_cmake_sources()
    externals, suspicious_comments, cleaned_kt = parse_kotlin_files()
    cpp_symbols, uncompiled_jni = parse_cpp_jni_symbols(compiled_sources)
    per_file_types, global_types = infer_kotlin_var_types(cleaned_kt)

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

    # 3. Externals sin llamador en Kotlin (resuelto por clase contenedora)
    externals_without_caller: List[KtExternal] = []
    for ext in externals:
        if count_kotlin_callers(ext, cleaned_kt, per_file_types, global_types) == 0:
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
