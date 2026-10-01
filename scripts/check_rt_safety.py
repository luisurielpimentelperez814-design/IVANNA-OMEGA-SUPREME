#!/usr/bin/env python3
"""
check_rt_safety.py — Fase A3 Auditor estático completo de seguridad Tiempo Real (RT-Safety).

Inspecciona todos los puntos de entrada de audio en tiempo real y sus callees
hasta 2 niveles de profundidad:
  - Los 11 puntos de entrada principales (omega_process, DSPBridge_nativeProcess,
    IvannaFusionEngine::process, DeclarativeUnifiedPipeline::process,
    OmniHolographicSingularityEngine::processStage,
    SupremeAcousticStabilityGuard::processBlock, RirConvolver::process,
    WfsRenderer::process, IntelligentUpmixer::processBlock,
    HoaBinauralDecoder::processBlock, ObjectRenderer::renderBlock)
  - HybridRenderer::renderBinaural y HybridRenderer::renderPlanar
  - RoomSimulator::processStereo
  - drainPendingDspParamsLocked
  - ClickFreeStageBase::process y cada etapa (onProcessWet) de omega_wave_stages.hpp

Si un punto de entrada no se encuentra, FALLA inmediatamente (prohibido omitir).
Prohíbe en funciones RT y sus callees (hasta nivel 2):
  - std::mutex / std::lock_guard / std::unique_lock bloqueante (std::try_to_lock permitido) / pthread_mutex_lock
  - new (no placement) / malloc / calloc / realloc / free
  - std::vector::resize / push_back / emplace_back / assign / clear()
  - std::string
  - throw
  - __android_log* / ALOG* / LOG*
"""

from __future__ import annotations
import re
import sys
from pathlib import Path
from typing import Dict, List, Set, Tuple

ROOT = Path(__file__).resolve().parent.parent
CPP_ROOT = ROOT / "app" / "src" / "main" / "cpp"

# 11 puntos de entrada base + HybridRenderer + RoomSimulator + drainPendingDspParamsLocked + 16 etapas
RT_ENTRY_TARGETS: List[Tuple[str, str]] = [
    ("omega_effect.cpp", "omega_process"),
    ("jni/ivanna_omega_jni.cpp", "Java_com_ivanna_omega_dsp_DSPBridge_nativeProcess"),
    ("jni/ivanna_omega_jni.cpp", "Java_com_ivanna_omega_core_IvannaNativeLib_nativeProcessBlock"),
    ("ivannalab/ivannalab.cpp", "IvannaLab::feed"),
    ("IvannaFusionCore.cpp", "IvannaFusionEngine::process"),
    ("include/omega_wave_stages.hpp", "DeclarativeUnifiedPipeline::process"),
    ("include/omega_wave_stages.hpp", "OmniHolographicSingularityEngine::processHolographicFusion"),
    ("supreme/SupremeAcousticStabilityGuard.hpp", "SupremeAcousticStabilityGuard::processBlock"),
    ("spatial/RirConvolver.cpp", "RirConvolver::process"),
    ("spatial/WfsRenderer.cpp", "WfsRenderer::process"),
    ("spatial/IntelligentUpmixer.cpp", "IntelligentUpmixer::processBlock"),
    ("spatial/HoaBinauralDecoder.cpp", "HoaBinauralDecoder::processBlock"),
    ("spatial/ivanna_object_renderer.cpp", "ObjectRenderer::renderBlock"),
    # Nuevos puntos requeridos por A3:
    ("spatial/HybridRenderer.hpp", "HybridRenderer::renderBinaural"),
    ("spatial/HybridRenderer.hpp", "HybridRenderer::renderPlanar"),
    ("spatial/RoomSimulator.hpp", "RoomSimulator::processStereo"),
    ("jni/ivanna_omega_jni.cpp", "drainPendingDspParamsLocked"),
    ("include/omega_wave_stages.hpp", "ClickFreeStageBase::process"),
    # Las 16 etapas de la cadena declarativa unificada:
    ("include/omega_wave_stages.hpp", "PhaseOracleControlStage::onProcessWet"),
    ("include/omega_wave_stages.hpp", "PsychoacousticsAnalysisStage::onProcessWet"),
    ("include/omega_wave_stages.hpp", "SofaSafAnalysisBridgeStage::onProcessWet"),
    ("include/omega_wave_stages.hpp", "VoiceProsodyStage::onProcessWet"),
    ("include/omega_wave_stages.hpp", "TinyMlClassifierStage::onProcessWet"),
    ("include/omega_wave_stages.hpp", "NeuromorphicTinyMlStage::onProcessWet"),
    ("include/omega_wave_stages.hpp", "LifNeuronPoolStage::onProcessWet"),
    ("include/omega_wave_stages.hpp", "AutonomousBrainStage::onProcessWet"),
    ("include/omega_wave_stages.hpp", "EvolutionaryEqStage::onProcessWet"),
    ("include/omega_wave_stages.hpp", "NeuralUpmixerStage::onProcessWet"),
    ("include/omega_wave_stages.hpp", "AntiDolbyClassicStage::onProcessWet"),
    ("include/omega_wave_stages.hpp", "AntiDolbyAiStage::onProcessWet"),
    ("include/omega_wave_stages.hpp", "AcousticSynthesisStage::onProcessWet"),
    ("include/omega_wave_stages.hpp", "SafOptimizerSuiteStage::onProcessWet"),
    ("include/omega_wave_stages.hpp", "CochlearPinnStage::onProcessWet"),
    ("include/omega_wave_stages.hpp", "NeuroCochlearManifoldStage::onProcessWet"),
]

FORBIDDEN_PATTERNS = [
    ("heap_alloc (new)", re.compile(r'\bnew\s+(?!\()[A-Za-z0-9_:]')),
    ("heap_alloc (malloc/free)", re.compile(r'\b(?:malloc|calloc|realloc|free)\s*\(')),
    (
        "vector_op (resize/push_back/assign/clear)",
        re.compile(r'\.\s*(?:resize|push_back|emplace_back|assign)\s*\(|\.\s*clear\s*\(\s*\)'),
    ),
    ("std::string", re.compile(r'\bstd::string\b')),
    (
        "android_log",
        re.compile(r'\b(?:__android_log_print|__android_log_write|ALOGI|ALOGW|ALOGE|ALOGD|LOGI|LOGW|LOGE)\s*\('),
    ),
    ("exception_throw", re.compile(r'\bthrow\b')),
]

MUTEX_LOCK_RE = re.compile(r'\b(?:std::mutex|std::lock_guard|std::unique_lock|pthread_mutex_lock)\b')


def strip_comments_and_strings(src: str) -> str:
    out = list(src)
    i = 0
    n = len(src)
    while i < n:
        if src.startswith("//", i):
            while i < n and src[i] != "\n":
                out[i] = " "
                i += 1
            continue
        if src.startswith("/*", i):
            out[i] = " "
            out[i + 1] = " "
            i += 2
            while i < n and not src.startswith("*/", i):
                if out[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                out[i] = " "
                out[i + 1] = " "
                i += 2
            continue
        if src[i] == '"':
            out[i] = " "
            i += 1
            while i < n and src[i] != '"':
                if src[i] == "\\" and i + 1 < n:
                    if out[i] != "\n":
                        out[i] = " "
                    i += 1
                if i < n and out[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                out[i] = " "
                i += 1
            continue
        i += 1
    return "".join(out)


def find_matching_brace(cleaned: str, brace_open: int) -> int:
    depth = 0
    i = brace_open
    n = len(cleaned)
    while i < n:
        if cleaned[i] == "{":
            depth += 1
        elif cleaned[i] == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def find_class_region(cleaned: str, class_name: str) -> Tuple[int, int] | None:
    pat = re.compile(r'\b(?:class|struct)\s+(?:alignas\s*\([^)]*\)\s*)?' + re.escape(class_name) + r'\b')
    for m in pat.finditer(cleaned):
        brace_open = cleaned.find("{", m.end())
        semi = cleaned.find(";", m.end())
        if brace_open == -1 or (semi != -1 and semi < brace_open):
            continue
        brace_close = find_matching_brace(cleaned, brace_open)
        if brace_close != -1:
            return (m.start(), brace_close + 1)
    return None


def extract_function_span(cleaned: str, func_name: str) -> Tuple[int, int] | None:
    """
    Devuelve (start_line_1based, end_line_1based) de la definición de `func_name`.
    Soporta:
      - Definiciones fuera de clase: `ClassName::method(...)`
      - Definiciones inline dentro de `class ClassName { ... method(...) { ... } }`
      - Funciones libres con declaraciones forward previas (ignora `;`, busca `{`).
    """
    search_start = 0
    search_end = len(cleaned)
    short_name = func_name.split("::")[-1]

    # 1. Intentar coincidencia exacta (p.ej. `ClassName::method` o función libre)
    exact_pat = re.compile(r'\b' + re.escape(func_name) + r'\s*\(')
    candidates = list(exact_pat.finditer(cleaned))

    # 2. Si tiene `ClassName::method` y no hay definición fuera de clase, buscar dentro de `class ClassName`
    if not candidates and "::" in func_name:
        cls_name = func_name.split("::")[0]
        region = find_class_region(cleaned, cls_name)
        if region is not None:
            search_start, search_end = region
            short_pat = re.compile(r'\b' + re.escape(short_name) + r'\s*\(')
            candidates = list(short_pat.finditer(cleaned, search_start, search_end))

    # 3. Fallback a búsqueda por nombre corto en todo el archivo
    if not candidates:
        short_pat = re.compile(r'\b' + re.escape(short_name) + r'\s*\(')
        candidates = list(short_pat.finditer(cleaned))

    for m in candidates:
        # Evitar llamadas a funciones (precedidas por `.` o `->`)
        prefix_idx = m.start() - 1
        while prefix_idx >= 0 and cleaned[prefix_idx].isspace():
            prefix_idx -= 1
        if prefix_idx >= 0 and cleaned[prefix_idx] in {".", ">"}:
            continue

        # Avanzar hasta cerrar paréntesis de parámetros `(...)`
        paren_open = cleaned.find("(", m.end() - 1)
        if paren_open == -1:
            continue
        p_depth = 0
        k = paren_open
        n = len(cleaned)
        while k < n:
            if cleaned[k] == "(":
                p_depth += 1
            elif cleaned[k] == ")":
                p_depth -= 1
                if p_depth == 0:
                    k += 1
                    break
            k += 1
        if p_depth != 0:
            continue

        # Tras `)`, saltar calificadores (`const`, `noexcept`, `override`, `final`) hasta `{` o `;`
        brace_open = cleaned.find("{", k)
        if brace_open == -1:
            continue
        semi = cleaned.find(";", k)
        if semi != -1 and semi < brace_open:
            # Es una declaración forward o una llamada a función; seguir buscando definición
            continue
        # Verificar que entre `)` y `{` solo haya calificadores válidos (no otra sentencia)
        between = cleaned[k:brace_open].strip()
        if any(bad in between for bad in ["=", "(", ")"]):
            continue

        brace_close = find_matching_brace(cleaned, brace_open)
        if brace_close != -1:
            start_line = cleaned.count("\n", 0, m.start()) + 1
            end_line = cleaned.count("\n", 0, brace_close) + 1
            return (start_line, end_line)

    return None


def find_direct_callees_in_span(cleaned: str, span: Tuple[int, int]) -> Set[str]:
    lines = cleaned.splitlines()
    body = "\n".join(lines[span[0] - 1 : span[1]])
    callees: Set[str] = set()
    ignore = {
        "if", "for", "while", "switch", "return", "sizeof", "alignas",
        "static_cast", "reinterpret_cast", "const_cast", "std", "clamp",
        "min", "max", "fabs", "abs", "sqrt", "sin", "cos", "tan", "exp", "log",
        "log10f", "powf", "isfinite", "isnan", "memcpy", "memset", "copysign",
        "fmod", "atan2", "floor", "ceil", "round", "fill", "load", "store",
        "exchange", "fetch_add", "fetch_or", "test_and_set", "clear",
    }
    # Llamadas directas o vía this-> / impl_-> en la misma clase/unidad de traducción
    for m in re.finditer(r'(?:(?<![.\w>])|(?:this|impl_)\s*->\s*)([A-Za-z_][A-Za-z0-9_]*)\s*\(', body):
        name = m.group(1)
        if name not in ignore:
            callees.add(name)
    return callees


def scan_span_for_violations(rel_path: str, cleaned: str, span: Tuple[int, int], context_label: str) -> List[str]:
    violations: List[str] = []
    lines = cleaned.splitlines()
    for line_no in range(span[0], span[1] + 1):
        line = lines[line_no - 1]
        # Regla especial para mutex/lock: try_to_lock está permitido
        if MUTEX_LOCK_RE.search(line):
            if "try_to_lock" not in line and "try_lock" not in line:
                violations.append(
                    f"  - {rel_path}:{line_no} [{context_label}] (blocking_mutex_lock): `{line.strip()}`"
                )
        for rule_name, regex in FORBIDDEN_PATTERNS:
            if regex.search(line):
                violations.append(
                    f"  - {rel_path}:{line_no} [{context_label}] ({rule_name}): `{line.strip()}`"
                )
    return violations


def main() -> int:
    print("=== [A3] CHECK RT-SAFETY STATIC REPORT ===")
    print(f"Analizando {len(RT_ENTRY_TARGETS)} puntos de entrada RT y sus callees hasta 2 niveles...")
    print()

    all_violations: List[str] = []
    analyzed_count = 0
    cleaned_cache: Dict[Path, str] = {}

    def get_cleaned(path: Path) -> str:
        if path not in cleaned_cache:
            raw = path.read_text(encoding="utf-8", errors="replace")
            cleaned_cache[path] = strip_comments_and_strings(raw)
        return cleaned_cache[path]

    for rel_cpp, func_name in RT_ENTRY_TARGETS:
        full_path = CPP_ROOT / rel_cpp
        rel_display = f"app/src/main/cpp/{rel_cpp}"
        if not full_path.exists():
            all_violations.append(f"  - [MISSING FILE] {rel_display} ({func_name})")
            continue
        cleaned = get_cleaned(full_path)
        span = extract_function_span(cleaned, func_name)
        if not span:
            all_violations.append(f"  - [MISSING ENTRY] {rel_display} -> no se encontró definición de `{func_name}`")
            continue

        analyzed_count += 1
        all_violations.extend(scan_span_for_violations(rel_display, cleaned, span, f"RT entry: {func_name}"))

        # Nivel 1 de callees
        lvl1_callees = find_direct_callees_in_span(cleaned, span)
        visited_callees: Set[str] = {func_name.split("::")[-1]}

        for c1 in sorted(lvl1_callees):
            if c1 in visited_callees:
                continue
            visited_callees.add(c1)
            c1_span = extract_function_span(cleaned, c1)
            if not c1_span or c1_span == span:
                continue
            all_violations.extend(
                scan_span_for_violations(rel_display, cleaned, c1_span, f"L1 callee `{c1}` from `{func_name}`")
            )

            # Nivel 2 de callees
            lvl2_callees = find_direct_callees_in_span(cleaned, c1_span)
            for c2 in sorted(lvl2_callees):
                if c2 in visited_callees:
                    continue
                visited_callees.add(c2)
                c2_span = extract_function_span(cleaned, c2)
                if not c2_span or c2_span in {span, c1_span}:
                    continue
                all_violations.extend(
                    scan_span_for_violations(
                        rel_display, cleaned, c2_span, f"L2 callee `{c2}` via `{c1}` from `{func_name}`"
                    )
                )

    print(f"Funciones RT verificadas: {analyzed_count}/{len(RT_ENTRY_TARGETS)}")
    if analyzed_count != len(RT_ENTRY_TARGETS):
        print(f"[FAIL] Solo se encontraron {analyzed_count}/{len(RT_ENTRY_TARGETS)} puntos de entrada RT.")
        for v in all_violations:
            print(v)
        return 1

    if all_violations:
        print(f"[FAIL] Violaciones de RT-Safety detectadas ({len(all_violations)}):")
        for v in all_violations:
            print(v)
        if "--ratchet" in sys.argv and len(all_violations) <= 3:
            print(f"[RATCHET-OK] Violaciones RT ({len(all_violations)}) <= baseline (3).")
            return 0
        return 1

    print("[PASS] 0 violaciones de RT-Safety en los puntos de entrada RT y callees de 1.er y 2.o nivel.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
