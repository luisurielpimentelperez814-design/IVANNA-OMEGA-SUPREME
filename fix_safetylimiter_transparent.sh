#!/data/data/com.termux/files/usr/bin/bash
set -e

FILE="app/src/main/cpp/dsp/SafetyLimiter.cpp"

python3 - <<'PY'
from pathlib import Path

p = Path("app/src/main/cpp/dsp/SafetyLimiter.cpp")
s = p.read_text()

old = """    // Lazy-init del lookahead si nunca se llamó setSampleRate().
    if (m_delayLen == 0) setSampleRate(m_sampleRate);
"""

new = """    // Transparencia absoluta: si el bloque completo está debajo
    // del threshold y no existe reducción activa, no tocar la señal.
    // Evita degradación por lookahead/release en material limpio.
    float inputPeak = 0.0f;

    for (int i = 0; i < frames; ++i) {
        if (std::isfinite(L[i]))
            inputPeak = std::max(inputPeak, std::fabs(L[i]));
        if (std::isfinite(R[i]))
            inputPeak = std::max(inputPeak, std::fabs(R[i]));
    }

    if (inputPeak <= m_threshold && m_gainNow >= 0.999f) {
        m_peakBefore.store(inputPeak, std::memory_order_relaxed);
        m_gainReduction.store(0.0f, std::memory_order_relaxed);
        return;
    }

    // Lazy-init del lookahead si nunca se llamó setSampleRate().
    if (m_delayLen == 0) setSampleRate(m_sampleRate);
"""

if old not in s:
    raise SystemExit("No se encontró el punto de inserción")

s=s.replace(old,new)

p.write_text(s)
PY

git diff -- app/src/main/cpp/dsp/SafetyLimiter.cpp

git add app/src/main/cpp/dsp/SafetyLimiter.cpp
git commit -m "fix(dsp): make SafetyLimiter transparent below threshold"
git push origin main
