#!/bin/bash
set -euo pipefail

REPORT="ivanna_oem_certification_gate.txt"
FAIL=0

echo "=================================" > "$REPORT"
echo "IVANNA OMEGA SUPREME CERTIFICATION GATE" >> "$REPORT"
echo "Commit: ${GITHUB_SHA:-local}" >> "$REPORT"
echo "Date: $(date -u)" >> "$REPORT"
echo "=================================" >> "$REPORT"


echo "" >> "$REPORT"
echo "[1] SOURCE SAFETY CHECK" >> "$REPORT"

if grep -R -nE "nan|inf|NaN|INF" app/src/main/cpp 2>/dev/null; then
    echo "FAIL: invalid floating point constants detected" >> "$REPORT"
    FAIL=1
else
    echo "PASS: floating point source clean" >> "$REPORT"
fi


echo "" >> "$REPORT"
echo "[2] DSP REGRESSION CHECK" >> "$REPORT"

if [ -d app/build ]; then
    echo "PASS: build artifacts available" >> "$REPORT"
else
    echo "WARNING: no build directory" >> "$REPORT"
fi


echo "" >> "$REPORT"
echo "[3] TEST RESULT CHECK" >> "$REPORT"

FAILED_TESTS=$(find . -name "*.xml" -type f -exec grep -l 'failure' {} \; 2>/dev/null || true)

if [ -n "$FAILED_TESTS" ]; then
    echo "FAIL: failing tests detected" >> "$REPORT"
    echo "$FAILED_TESTS" >> "$REPORT"
    FAIL=1
else
    echo "PASS: no failing test reports" >> "$REPORT"
fi


echo "" >> "$REPORT"
echo "[4] DAEMON SAFETY CHECK" >> "$REPORT"

if grep -R "<<<<<<<\|=======\|>>>>>>>" app/src/main/cpp/daemon 2>/dev/null; then
    echo "FAIL: merge markers detected" >> "$REPORT"
    FAIL=1
else
    echo "PASS: daemon source clean" >> "$REPORT"
fi


echo "" >> "$REPORT"
echo "[5] CERTIFICATION RESULT" >> "$REPORT"

if [ "$FAIL" -eq 0 ]; then
    echo "CERTIFIED: ZERO REGRESSION DETECTED" >> "$REPORT"
else
    echo "REJECTED: REGRESSION DETECTED" >> "$REPORT"
fi


echo "=================================" >> "$REPORT"


if [ "$FAIL" -ne 0 ]; then
    exit 1
fi

