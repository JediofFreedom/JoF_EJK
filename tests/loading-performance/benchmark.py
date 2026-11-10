"""Alternate Release reference/candidate runs and report median CPU-path timings.

Usage: python benchmark.py reference.exe candidate.exe fixture.pk3 [assets0.pk3 ...]
Both executables must be built from this harness (use -DREFERENCE_REF for reference).
"""
import json
import re
import statistics
import subprocess
import sys


def run(executable, packages):
    output = subprocess.check_output([executable, *packages], text=True)
    timings = {}
    for renderer, elapsed in re.findall(r"(vanilla|rend2|dedicated): 400 misses in ([\d.]+) ms", output):
        timings[f"{renderer} shader misses"] = float(elapsed)
    timings["file listings"] = float(re.search(r"filesystem: .* in ([\d.]+) ms", output)[1])
    mounts = re.findall(r"mount: (.*): \d+ entries, ([\d.]+) ms, checksums (\w+/\w+)", output)
    timings["package mounting (total)"] = sum(float(mount[1]) for mount in mounts)
    checksums = {mount[0]: mount[2] for mount in mounts}
    return timings, checksums


reference, candidate, *packages = sys.argv[1:]
if not packages:
    raise SystemExit(__doc__)
measurements = {"reference": [], "candidate": []}
for iteration in range(6):  # First pair warms the OS file cache.
    order = [("reference", reference), ("candidate", candidate)]
    if iteration % 2:
        order.reverse()
    checksums = {}
    for name, executable in order:
        timings, checksums[name] = run(executable, packages)
        if iteration:
            measurements[name].append(timings)
    if checksums["reference"] != checksums["candidate"]:
        raise SystemExit("Package checksums changed!")

report = {}
for metric in measurements["reference"][0]:
    before = statistics.median(row[metric] for row in measurements["reference"])
    after = statistics.median(row[metric] for row in measurements["candidate"])
    report[metric] = {"reference_ms": round(before, 3), "candidate_ms": round(after, 3),
                      "speedup": round(before / after, 2)}
print(json.dumps({"measured_runs": 5, "checksums_identical": True, "medians": report}, indent=2))
