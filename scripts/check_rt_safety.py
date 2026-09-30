#!/usr/bin/env python3
"""
check_rt_safety.py — Fase 0.3 Auditor estático de seguridad Tiempo Real (RT-Safety).

Inspecciona las funciones de callback de audio en tiempo real (`omega_process`,
`DSPBridge_nativeProcess`, `IvannaFusionEngine::process`, `DeclarativeUnifiedPipeline::process`,
`RirConvolver::process`, `WfsRenderer::process`, `IntelligentUpmixer::process`,
`HoaBinauralDecoder::decode`, `ObjectRenderer::process`, `SupremeAcousticStabilityGuard::processOutputBlock`)
y sus callees directos de primer nivel (como `omega_apply_room`, etc.).

Falla (exit 1) si el cuerpo de una función RT o un callee directo de 1.er nivel contiene:
  - std::mutex / std::lock_guard / std::unique_lock / pthread_mutex_lock
  - new (no placement) / malloc / calloc / realloc / free
  - .resize( / .push_back( / .emplace_back(
  - std::string
  - __android_log_print / __android_log_write / ALOG*
  - throw
"""

from __future__ import annotations
import re
import sys
from pathlib import Path
from typing import Dict, List, Set, Tuple

ROOT = Path(__file__).resolve().parent.parent
CPP_ROOT = ROOT / "app" / "src" / "main" / "cpp"

RT_ENTRY_TARGETS = [
    ("omega_effect.cpp", "omega_process"),
    ("jni/ivanna_omega_jni.cpp", "Java_com_ivanna_omega_dsp_DSPBridge_nativeProcess"),
    ("IvannaFusionCore.cpp", "IvannaFusionEngine::process"),
    ("include/omega_unified_dsp_stage.hpp", "DeclarativeUnifiedPipeline::process"),
    ("include/omega_wave_stages.hpp", "OmniHolographicSingularityEngine::processStage"),
    ("supreme/SupremeAcousticStabilityGuard.hpp", "SupremeAcousticStabilityGuard::processOutputBlock"),
    ("spatial/RirConvolver.cpp", "RirConvolver::process"),
    ("spatial/WfsRenderer.cpp", "WfsRenderer::process"),
    ("spatial/IntelligentUpmixer.cpp", "IntelligentUpmixer::process"),
    ("spatial/HoaBinauralDecoder.cpp", "HoaBinauralDecoder::decode"),
    ("spatial/ivanna_object_renderer.cpp", "ObjectRenderer::process"),
]

FORBIDDEN_PATTERNS = [
    ("mutex/lock", re.compile(r'\b(?:std::mutex|std::lock_guard|std::unique_lock|pthread_mutex_lock)\b')),
    ("heap_alloc (new)", re.compile(r'\bnew\s+(?!\()[A-Za-z0-9_:]')),
    ("heap_alloc (malloc/free)", re.compile(r'\b(?:malloc|calloc|realloc|free)\s*\(')),
    ("vector_alloc (resize/push_back)", re.compile(r'\.\s*(?:resize|push_back|emplace_back)\s*\(')),
    ("std::string", re.compile(r'\bstd::string\b')),
    ("android_log", re.compile(r'\b(?:__android_log_print|__android_log_write|ALOGI|ALOGW|ALOGE|ALOGD|LOGI|LOGW|LOGE)\s*\(')),
    ("exception_throw", re.compile(r'\bthrow\b')),
]


def strip_comments_and_strings(src: str) -> str:
    out = list(src)
    i = 0
    n = len(src)
    while i < n:
        if src.startswith("//", i):
            while i < n and src[i] != '\n':
                out[i] = ' '
                i += 1
            continue
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
        i += 1
    return "".join(out)


def extract_function_span(cleaned: str, func_name: str) -> Tuple[int, int] | None:
    """Devuelve (start_line_1based, end_line_1based) del cuerpo de `func_name`."""
    short_name = func_name.split("::")[-1]
    pattern = re.compile(r'\b' + re.escape(func_name) + r'\s*\(')
    m = pattern.search(cleaned)
    if not m:
        pattern = re.compile(r'\b' + re.escape(short_name) + r'\s*\([^;{]*\)\s*(?:noexcept\s*)?(?:override\s*)?\{')
        m = pattern.search(cleaned)
        if not m:
            return None

    # Avanzar hasta la primera '{' de apertura del cuerpo
    pos = m.start()
    brace_open = cleaned.find('{', m.end() - 1)
    if brace_open == -1:
        return None
    # Verificar que no haya ';' antes de '{' (declaración forward)
    semi = cleaned.find(';', m.end() - 1)
    if semi != -1 and semi < brace_open:
        return None

    depth = 0
    i = brace_open
    n = len(cleaned)
    while i < n:
        if cleaned[i] == '{':
            depth += 1
        elif cleaned[i] == '}':
            depth -= 1
            if depth == 0:
                start_line = cleaned.count('\n', 0, pos) + 1
                end_line = cleaned.count('\n', 0, i) + 1
                return (start_line, end_line)
        i += 1
    return None


def find_direct_callees_in_file(cleaned: str, span: Tuple[int, int]) -> Set[str]:
    lines = cleaned.splitlines()
    body = "\n".join(lines[span[0]-1:span[1]])
    callees: Set[str] = set()
    ignore = {
        "if", "for", "while", "switch", "return", "sizeof", "alignas",
        "static_cast", "reinterpret_cast", "const_cast", "std", "clamp",
        "min", "max", "fabs", "sqrt", "sin", "cos", "tan", "exp", "log",
        "log10f", "powf", "isfinite", "isnan", "memcpy", "memset",
    }
    for m in re.finditer(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*\(', body):
        name = m.group(1)
        if name not in ignore:
            callees.add(name)
    return callees


def scan_span_for_violations(rel_path: str, cleaned: str, span: Tuple[int, int], context_label: str) -> List[str]:
    violations: List[str] = []
    lines = cleaned.splitlines()
    for line_no in range(span[0], span[1] + 1):
        line = lines[line_no - 1]
        for rule_name, regex in FORBIDDEN_PATTERNS:
            if regex.search(line):
                violations.append(
                    f"  - {rel_path}:{line_no} [{context_label}] ({rule_name}): `{line.strip()}`"
                )
    return violations


def main() -> int:
    print("=== [0.3] CHECK RT-SAFETY STATIC REPORT ===")
    print(f"Analizando {len(RT_ENTRY_TARGETS)} puntos de entrada RT y sus callees de 1.er nivel...")
    print()

    all_violations: List[str] = []
    analyzed_count = 0

    for rel_cpp, func_name in RT_ENTRY_TARGETS:
        full_path = CPP_ROOT / rel_cpp
        if not full_path.exists():
            all_violations.append(f"  - [MISSING FILE] app/src/main/cpp/{rel_cpp} ({func_name})")
            continue
        raw = full_path.read_text(encoding="utf-8", errors="replace")
        cleaned = strip_comments_and_strings(raw)
        span = extract_function_span(cleaned, func_name)
        if not span:
            continue
        analyzed_count += 1
        rel_display = f"app/src/main/cpp/{rel_cpp}"
        all_violations.extend(scan_span_for_violations(rel_display, cleaned, span, f"RT entry: {func_name}"))

        # Analizar callees de primer nivel definidos en el mismo archivo
        callees = find_direct_callees_in_file(cleaned, span)
        for callee in sorted(callees):
            if callee == func_name.split("::")[-1]:
                continue
            c_span = extract_function_span(cleaned, callee)
            if c_span and c_span != span:
                all_violations.extend(
                    scan_span_for_violations(rel_display, cleaned, c_span, f"1st-level callee `{callee}` from `{func_name}`")
                )

    print(f"Funciones RT verificadas: {analyzed_count}/{len(RT_ENTRY_TARGETS)}")
    if all_violations:
        print(f"[FAIL] Violaciones de RT-Safety detectadas ({len(all_violations)}):")
        for v in all_violations:
            print(v)
        if "--ratchet" in sys.argv and len(all_violations) <= 3:
            print(f"[RATCHET-OK] Violaciones RT ({len(all_violations)}) <= baseline (3).")
            return 0
        return 1

    print("[PASS] 0 violaciones de RT-Safety en funciones RT y callees de 1.er nivel.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
