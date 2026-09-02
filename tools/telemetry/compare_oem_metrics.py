import json
import sys

current=sys.argv[1]
previous=sys.argv[2]

with open(current) as f:
    c=json.load(f)

with open(previous) as f:
    p=json.load(f)


metrics=[
    "latency_samples",
    "build_size_kb",
    "cpu_percent",
    "memory_kb"
]


print("IVANNA OEM PERFORMANCE DELTA")

for m in metrics:
    old=p["metrics"].get(m,0)
    new=c["metrics"].get(m,0)

    delta=new-old

    print(
        f"{m}: "
        f"{old} -> {new} "
        f"delta={delta}"
    )

    if m=="latency_samples" and delta>0:
        print("WARNING latency regression")

    if m=="memory_kb" and delta>10240:
        print("WARNING memory growth")


