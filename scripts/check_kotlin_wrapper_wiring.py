#!/usr/bin/env python3
"""
check_kotlin_wrapper_wiring.py — Fase A2 Auditor de cableado completo de wrappers Kotlin -> JNI.

Para cada `fun` pública (no `private`, no `protected`, no `external`, no `override`)
cuyo cuerpo invoca al menos un `external fun` (directamente en su misma clase o
cualificado como `ClaseNativa.externalFn(...)`), exige >= 1 llamador real en Kotlin
(UI, worker, ViewModel, servicio u otro consumidor activo).

Falla (exit 1) con la lista de wrappers sin llamador.
"""

from __future__ import annotations
import re
import sys
from pathlib import Path
from typing import Dict, List, Set, Tuple

from check_jni_wiring import (
    KT_ROOT,
    ROOT,
    KtExternal,
    infer_kotlin_var_types,
    parse_kotlin_files,
)


class KtWrapper:
    def __init__(
        self,
        pkg: str,
        owner_class: str,
        name: str,
        rel_file: Path,
        line: int,
        span: Tuple[int, int],
        class_span: Tuple[int, int],
        called_externals: List[str],
    ):
        self.pkg = pkg
        self.owner_class = owner_class
        self.name = name
        self.rel_file = rel_file
        self.line = line
        self.span = span
        self.class_span = class_span
        self.called_externals = called_externals

    @property
    def qualified_name(self) -> str:
        return f"{self.pkg}.{self.owner_class}.{self.name}"


def find_public_wrappers(
    externals: List[KtExternal],
    cleaned_kt: Dict[str, str],
    per_file_types: Dict[str, Dict[str, Set[str]]],
    global_types: Dict[str, Set[str]],
) -> List[KtWrapper]:
    # Mapa de external methods por clase contenedora
    exts_by_class: Dict[str, Set[str]] = {}
    all_ext_methods: Dict[str, Set[str]] = {}  # method -> set of owner_classes
    for ext in externals:
        exts_by_class.setdefault(ext.owner_class, set()).add(ext.method)
        all_ext_methods.setdefault(ext.method, set()).add(ext.owner_class)

    wrappers: List[KtWrapper] = []

    fun_decl_re = re.compile(
        r'^\s*(?:@[A-Za-z0-9_]+(?:\([^)]*\))?\s*)*'
        r'((?:(?:public|internal|private|protected|override|inline|suspend|operator|infix|tailrec|external)\s+)*)'
        r'fun\s+(?:<[A-Za-z0-9_,\s:*]+>\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*\('
    )
    scope_token_re = re.compile(
        r'\b(?:companion\s+object|class\s+[A-Za-z0-9_]+|object\s+[A-Za-z0-9_]+|interface\s+[A-Za-z0-9_]+)|\bfun\b|\{|\}'
    )

    for rel_str, cleaned in sorted(cleaned_kt.items()):
        rel_path = Path(rel_str)
        lines = cleaned.splitlines()
        total_lines = len(lines)
        pkg_m = re.search(r'^\s*package\s+([A-Za-z0-9_.]+)', cleaned, re.MULTILINE)
        pkg = pkg_m.group(1) if pkg_m else ""

        # Primer pase: determinar class_span por línea
        brace_depth = 0
        scope_stack: List[List[object]] = []
        pending_scope: Tuple[str, int] | None = None
        line_owner: List[Tuple[str, int, int]] = [(rel_path.stem + "Kt", 1, total_lines)] * (total_lines + 1)
        closed_scopes: List[Tuple[str, int, int]] = []

        for idx, line in enumerate(lines, start=1):
            for tok in scope_token_re.finditer(line):
                t = tok.group(0)
                if t == "fun":
                    pending_scope = None
                elif t == "{":
                    brace_depth += 1
                    if pending_scope is not None:
                        scope_stack.append([pending_scope[0], brace_depth, pending_scope[1]])
                        pending_scope = None
                elif t == "}":
                    while scope_stack and scope_stack[-1][1] == brace_depth:
                        s_name, _, s_start = scope_stack.pop()
                        if s_name != "Companion":
                            closed_scopes.append((str(s_name), int(s_start), idx))
                    brace_depth = max(0, brace_depth - 1)
                elif t.startswith("companion"):
                    pending_scope = ("Companion", idx)
                else:
                    parts = t.split()
                    if len(parts) == 2 and parts[1] not in {"by", "where"}:
                        pending_scope = (parts[1], idx)

        while scope_stack:
            s_name, _, s_start = scope_stack.pop()
            if s_name != "Companion":
                closed_scopes.append((str(s_name), int(s_start), total_lines))

        # Ordenar del ámbito más externo al más interno (mayor span primero)
        for s_name, s_start, s_end in sorted(closed_scopes, key=lambda x: (x[2] - x[1]), reverse=True):
            for lno in range(s_start, s_end + 1):
                line_owner[lno] = (s_name, s_start, s_end)

        # Segundo pase: localizar declaraciones `fun` públicas (excluyendo UI @Composable)
        idx = 1
        prev_was_composable = False
        while idx <= total_lines:
            line = lines[idx - 1]
            if re.search(r'@Composable\b', line) and not re.search(r'\bfun\s+', line):
                prev_was_composable = True
                idx += 1
                continue
            m_fun = fun_decl_re.match(line)
            if not m_fun:
                if line.strip():
                    prev_was_composable = False
                idx += 1
                continue
            is_composable = prev_was_composable or bool(re.search(r'@Composable\b', line))
            prev_was_composable = False
            mods = m_fun.group(1) or ""
            fn_name = m_fun.group(2)
            mod_set = set(mods.split())
            if is_composable or (mod_set & {"private", "protected", "external", "override"}):
                idx += 1
                continue

            owner_cls, cls_start, cls_end = line_owner[idx]

            # Encontrar el final de la lista de parámetros ')' y el cuerpo ('{' o '=')
            # Escaneamos desde idx hasta encontrar '{' o '=' a nivel de paréntesis 0
            paren_depth = 0
            sig_line = idx
            body_start_line = idx
            is_block_body = False
            found_body = False
            scan_line = idx
            while scan_line <= min(total_lines, idx + 15):
                sline = lines[scan_line - 1]
                start_col = m_fun.end() - 1 if scan_line == idx else 0
                j = start_col
                while j < len(sline):
                    ch = sline[j]
                    if ch == "(":
                        paren_depth += 1
                    elif ch == ")":
                        paren_depth = max(0, paren_depth - 1)
                    elif paren_depth == 0 and ch == "{":
                        is_block_body = True
                        found_body = True
                        body_start_line = scan_line
                        break
                    elif paren_depth == 0 and ch == "=" and (j + 1 >= len(sline) or sline[j + 1] != "="):
                        if j == 0 or sline[j - 1] not in {"!", "<", ">", "="}:
                            is_block_body = False
                            found_body = True
                            body_start_line = scan_line
                            break
                    j += 1
                if found_body:
                    break
                scan_line += 1

            if not found_body:
                idx += 1
                continue

            if is_block_body:
                # Contar llaves desde body_start_line hasta cerrar el bloque de la función
                b_depth = 0
                end_line = body_start_line
                started = False
                for lno in range(body_start_line, cls_end + 1):
                    sline = lines[lno - 1]
                    for ch in sline:
                        if ch == "{":
                            b_depth += 1
                            started = True
                        elif ch == "}":
                            b_depth -= 1
                            if started and b_depth == 0:
                                end_line = lno
                                break
                    if started and b_depth == 0:
                        break
            else:
                # Expression body (`fun foo() = ...`): continúa mientras las líneas siguientes
                # no inicien otra declaración o cierren la clase
                end_line = body_start_line
                b_depth = 0
                for ch in lines[body_start_line - 1]:
                    if ch in "{(":
                        b_depth += 1
                    elif ch in "})":
                        b_depth = max(0, b_depth - 1)
                for lno in range(body_start_line + 1, min(cls_end, body_start_line + 25) + 1):
                    sline = lines[lno - 1].strip()
                    if b_depth == 0 and (
                        not sline
                        or re.match(r'^(?:@(?:JvmStatic|Composable)\b|(?:public|internal|private|protected|override|external|inline|suspend|fun|val|var|class|object|companion)\b|\})', sline)
                    ):
                        break
                    end_line = lno
                    for ch in sline:
                        if ch in "{(":
                            b_depth += 1
                        elif ch in "})":
                            b_depth = max(0, b_depth - 1)

            body_text = "\n".join(lines[idx - 1 : end_line])
            called_exts: List[str] = []
            f_types = per_file_types.get(rel_str, {})

            # ¿Invoca algún external de su propia clase?
            for ext_m in exts_by_class.get(owner_cls, set()):
                if re.search(r'\b' + re.escape(ext_m) + r'\s*\(', body_text):
                    called_exts.append(f"{owner_cls}.{ext_m}")

            # ¿Invoca algún external cualificado `ClaseNativa.ext(` o `recv.ext(`?
            for ext_m, ext_owners in all_ext_methods.items():
                if ext_m not in body_text:
                    continue
                for ext_cls in ext_owners:
                    if re.search(
                        r'\b' + re.escape(ext_cls) + r'\s*(?:\.\s*Companion\s*)?(?:\?\.|\.)\s*' + re.escape(ext_m) + r'\s*\(',
                        body_text,
                    ):
                        tag = f"{ext_cls}.{ext_m}"
                        if tag not in called_exts:
                            called_exts.append(tag)
                    for m_recv in re.finditer(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*(?:\?\.|\.)\s*' + re.escape(ext_m) + r'\s*\(', body_text):
                        recv = m_recv.group(1)
                        known = f_types.get(recv) or global_types.get(recv, set())
                        if ext_cls in known:
                            tag = f"{ext_cls}.{ext_m}"
                            if tag not in called_exts:
                                called_exts.append(tag)

            if called_exts:
                wrappers.append(
                    KtWrapper(
                        pkg=pkg,
                        owner_class=owner_cls,
                        name=fn_name,
                        rel_file=rel_path,
                        line=idx,
                        span=(idx, end_line),
                        class_span=(cls_start, cls_end),
                        called_externals=sorted(called_exts),
                    )
                )

            idx = max(idx + 1, end_line + 1)

    return wrappers


def count_wrapper_callers(
    w: KtWrapper,
    cleaned_kt: Dict[str, str],
    per_file_types: Dict[str, Dict[str, Set[str]]],
    global_types: Dict[str, Set[str]],
) -> int:
    cls = w.owner_class
    method = w.name
    w_file = str(w.rel_file)
    w_start, w_end = w.span
    cls_start, cls_end = w.class_span

    decl_fun_re = re.compile(r'\bfun\s+(?:<[A-Za-z0-9_,\s:*]+>\s+)?' + re.escape(method) + r'\s*\(')
    qual_cls_re = re.compile(
        r'\b' + re.escape(cls) + r'\s*(?:\.\s*(?:Companion|shared|instance)\s*)?(?:\?\.|\.)\s*' + re.escape(method) + r'\s*\('
    )
    qual_recv_re = re.compile(
        r'\b([A-Za-z_][A-Za-z0-9_]*)\s*(?:\?\.|\.)\s*' + re.escape(method) + r'\s*\('
    )
    unqual_or_self_re = re.compile(
        r'(?:(?<![.\w?])|(?:this|Companion|shared)\s*\.\s*)' + re.escape(method) + r'\s*\('
    )

    total = 0
    for rel_path, content in cleaned_kt.items():
        if method not in content:
            continue
        f_types = per_file_types.get(rel_path, {})
        lines = content.splitlines()
        for line_no, line in enumerate(lines, start=1):
            if method not in line:
                continue
            # Excluir el propio cuerpo/declaración del wrapper
            if rel_path == w_file and w_start <= line_no <= w_end:
                continue
            line_wo_decl = decl_fun_re.sub(" ", line)

            # 1) Llamada cualificada `Clase.wrapper(` o `Clase.shared.wrapper(`
            c_matches = len(qual_cls_re.findall(line_wo_decl))
            if c_matches > 0:
                total += c_matches

            # 2) Llamada por receptor `obj.wrapper(` con tipo conocido == cls
            for m_recv in qual_recv_re.finditer(line_wo_decl):
                recv = m_recv.group(1)
                if recv in {cls, "Companion", "this", "shared", "instance"}:
                    continue
                known_types = f_types.get(recv) or global_types.get(recv, set())
                if cls in known_types:
                    total += 1

            # 3) Llamada dentro de la misma clase contenedora fuera del propio wrapper
            if rel_path == w_file and cls_start <= line_no <= cls_end:
                line_for_self = qual_cls_re.sub(" ", line_wo_decl)
                total += len(unqual_or_self_re.findall(line_for_self))

            # 4) Función top-level (sin clase contenedora)
            if cls.endswith("Kt") and rel_path != w_file:
                total += len(unqual_or_self_re.findall(line_wo_decl))

    return total


def main() -> int:
    externals, _, cleaned_kt = parse_kotlin_files()
    per_file_types, global_types = infer_kotlin_var_types(cleaned_kt)
    wrappers = find_public_wrappers(externals, cleaned_kt, per_file_types, global_types)

    uncalled: List[KtWrapper] = []
    for w in wrappers:
        if count_wrapper_callers(w, cleaned_kt, per_file_types, global_types) == 0:
            uncalled.append(w)

    print("=== [A2] CHECK KOTLIN WRAPPER WIRING REPORT ===")
    print(f"Total public Kotlin JNI wrappers : {len(wrappers)}")
    print(f"Uncalled public JNI wrappers     : {len(uncalled)}")
    print()

    if uncalled:
        print(f"[FAIL] Wrappers Kotlin públicos sin llamador ({len(uncalled)}):")
        for w in uncalled:
            exts_str = ", ".join(w.called_externals)
            print(f"  - {w.rel_file}:{w.line} -> {w.qualified_name} (invoca: {exts_str})")
        return 1

    print("[PASS] Todos los wrappers públicos que invocan JNI tienen >= 1 llamador en Kotlin.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
