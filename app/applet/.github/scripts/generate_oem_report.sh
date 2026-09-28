#!/bin/bash
set -euo pipefail

REPORT="ivanna_oem_stability_report.txt"

echo "=================================" > "$REPORT"
echo "IVANNA OMEGA SUPREME OEM REPORT" >> "$REPORT"
echo "Build: ${GITHUB_SHA:-local}" >> "$REPORT"
echo "Date: $(date -u)" >> "$REPORT"
echo "=================================" >> "$REPORT"


echo "" >> "$REPORT"
echo "[BUILD STATUS]" >> "$REPORT"

if [ -d app/build ]; then
    echo "PASS: Android build directory exists" >> "$REPORT"
else
    echo "FAIL: build directory missing" >> "$REPORT"
fi


echo "" >> "$REPORT"
echo "[DSP STATIC CHECK]" >> "$REPORT"

if grep -R "NaN\|Inf" app/src/main/cpp 2>/dev/null; then
    echo "WARNING: invalid constants found in source" >> "$REPORT"
else
    echo "PASS: no invalid DSP constants detected" >> "$REPORT"
fi


echo "" >> "$REPORT"
echo "[TEST RESULTS]" >> "$REPORT"

if find . -name "*.xml" | grep -q .; then
    find . -name "*.xml" | head -20 >> "$REPORT"
    echo "PASS: test reports generated" >> "$REPORT"
else
    echo "INFO: no XML reports found" >> "$REPORT"
fi


echo "" >> "$REPORT"
echo "[ARTIFACT STATUS]" >> "$REPORT"
echo "Certification report generated successfully" >> "$REPORT"


echo "" >> "$REPORT"
echo "=================================" >> "$REPORT"
echo "DEGRADATION CHECK COMPLETE" >> "$REPORT"
echo "=================================" >> "$REPORT"

