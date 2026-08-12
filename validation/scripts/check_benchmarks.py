#!/usr/bin/env python3
import json, sys, os

# Umbrales máximos en ns (ajustar según benchmarks reales)
BUDGETS = {
    "BM_Compressor/64": 50000,
    "BM_ParametricEQ/64": 80000,
    "BM_HarmonicExciter/64": 60000,
    "BM_StereoWidener/64": 40000,
    "BM_SafetyLimiter/64": 30000,
}

def main(path):
    if not os.path.exists(path):
        print(f"⚠️  {path} no existe; omitiendo verificación")
        return 0
    with open(path) as f:
        data = json.load(f)
    failures = []
    for bench in data.get('benchmarks', []):
        name = bench.get('name', '')
        # Extraer nombre base y tamaño de frame
        base_name = name.rsplit('/', 1)[0] if '/' in name else name
        if base_name in BUDGETS:
            time_ns = bench.get('cpu_time', 0)
            if time_ns > BUDGETS[base_name]:
                failures.append(f"{name}: {time_ns}ns > {BUDGETS[base_name]}ns")
    if failures:
        print("❌ Performance budgets exceeded:")
        for f_ in failures:
            print("  -", f_)
        return 1
    print("✅ All performance budgets met")
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'bench.json'))
