#!/data/data/com.termux/files/usr/bin/bash
set -e

FILE="app/src/main/cpp/dsp/SafetyLimiter.cpp"

python3 - <<'PY'
from pathlib import Path

p = Path("app/src/main/cpp/dsp/SafetyLimiter.cpp")
s = p.read_text()

s = s.replace(
"""    const float excess  = peakLin - m_threshold;
    float limited = m_threshold + excess * kKneeRatio;
    if (limited > m_ceiling) limited = m_ceiling;
    return (peakLin > 1e-9f) ? (limited / peakLin) : 1.0f;
""",
"""    const float excess = peakLin - m_threshold;

    // Curva suave hacia ceiling para reducir THD.
    // Evita que el knee genere una discontinuidad fuerte.
    float limited = m_threshold +
                    excess / (1.0f + excess * 8.0f);

    if (limited > m_ceiling)
        limited = m_ceiling;

    return (peakLin > 1e-9f) ? (limited / peakLin) : 1.0f;
"""
)

s = s.replace(
"""    float limited = m_threshold + (ax - m_threshold) * kKneeRatio;
    if (limited > m_ceiling) {
""",
"""    float excess = ax - m_threshold;

    float limited = m_threshold +
                    excess / (1.0f + excess * 8.0f);

    if (limited > m_ceiling) {
"""
)

p.write_text(s)
PY

git diff -- app/src/main/cpp/dsp/SafetyLimiter.cpp

git add app/src/main/cpp/dsp/SafetyLimiter.cpp
git commit -m "fix(dsp): reduce SafetyLimiter THD with smooth knee curve"
git push origin main
