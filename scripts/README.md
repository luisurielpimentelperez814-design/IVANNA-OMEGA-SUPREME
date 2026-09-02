# scripts/

## benchmark_device.sh

Mide en dispositivo real las métricas que el README reclama pero que no tenían evidencia empírica:

| Métrica | Umbral | Cómo se mide |
|---|---|---|
| Latencia AudioFlinger | ≤ 5 ms | `dumpsys media.audio_flinger` |
| CPU daemon (avg 5 s) | ≤ 5% | `/proc/PID/stat` × 5 muestras |
| RAM daemon (VmRSS) | ≤ 8192 kB | `/proc/PID/status` |
| Clip count SafetyLimiter | 0 | `ivanna_control.sh telemetry` |
| Socket round-trip | informativo | `nc -U @omega_daemon_socket` |
| Temperatura SoC | informativa | `/sys/class/thermal/zone*/temp` |

**Uso:**

```bash
adb shell su -c "sh /data/adb/modules/ivanna_omega_supreme/scripts/benchmark_device.sh"
adb pull /data/adb/ivanna_omega/benchmark_<timestamp>.json .
```

**Salida:** JSON en `/data/adb/ivanna_omega/benchmark_<unix_timestamp>.json`

```json
{
  "ivanna_benchmark": {
    "version": "1.0",
    "timestamp_unix": 1723492800,
    "device": { "model": "Xiaomi 13T Pro", "android": "14", "soc": "qcom" },
    "daemon": {
      "cpu_avg_pct": 1.2,
      "ram_kb": 3840,
      "clip_count": 0,
      "socket_roundtrip_us": 180
    },
    "audio": { "audioflinger_latency_ms": 4.5 },
    "pass": {
      "latency": true,
      "cpu": true,
      "ram": true,
      "clips": true
    }
  }
}
```

El script no modifica nada del sistema — es solo lectura.
