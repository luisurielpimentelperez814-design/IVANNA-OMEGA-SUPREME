#!/system/bin/sh

REPORT="ivanna_oem_stability_report.txt"

echo "=================================" > $REPORT
echo "IVANNA OMEGA SUPREME OEM REPORT" >> $REPORT
echo "Generated: $(date)" >> $REPORT
echo "=================================" >> $REPORT


echo "" >> $REPORT
echo "[DSP HEALTH]" >> $REPORT

if grep -R "nan\|NaN\|inf\|Inf" logs/ 2>/dev/null; then
    echo "FAIL: invalid floating values detected" >> $REPORT
else
    echo "PASS: zero NaN/Inf events" >> $REPORT
fi


echo "" >> $REPORT
echo "[CLIPPING]" >> $REPORT

if grep -R "clip\|overflow" logs/ 2>/dev/null; then
    echo "WARNING: clipping events detected" >> $REPORT
else
    echo "PASS: zero clipping events" >> $REPORT
fi


echo "" >> $REPORT
echo "[DAEMON]" >> $REPORT

if grep -R "crash\|fatal\|abort" logs/ 2>/dev/null; then
    echo "FAIL: daemon instability detected" >> $REPORT
else
    echo "PASS: daemon stable" >> $REPORT
fi


echo "" >> $REPORT
echo "[MEMORY]" >> $REPORT

echo "Memory snapshot:" >> $REPORT
dumpsys meminfo com.ivanna.omega 2>/dev/null >> $REPORT


echo "" >> $REPORT
echo "[CPU]" >> $REPORT

top -n 1 2>/dev/null | grep ivanna >> $REPORT


echo "" >> $REPORT
echo "[LATENCY]" >> $REPORT

if [ -f latency.log ]; then
    cat latency.log >> $REPORT
else
    echo "No latency log available" >> $REPORT
fi


echo "" >> $REPORT
echo "=================================" >> $REPORT
echo "RESULT: DEGRADATION CHECK COMPLETE" >> $REPORT
echo "=================================" >> $REPORT

