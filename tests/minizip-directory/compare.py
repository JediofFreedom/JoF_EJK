"""Compare observable parser results, then optionally time installed archives."""
import argparse
import glob
import json
import pathlib
import statistics
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("reference")
parser.add_argument("candidate")
parser.add_argument("fixtures")
parser.add_argument("archives", nargs="*")
parser.add_argument("--runs", type=int, default=7)
parser.add_argument("--extract-installed", action="store_true")
args = parser.parse_args()

def run(executable, paths, extract=False, check=False):
    command = [executable] + (["--extract"] if extract else []) + (["--check"] if check else []) + list(map(str, paths))
    return json.loads(subprocess.check_output(command, text=True))

def same(left, right):
    keys = ("archives", "entries", "fingerprint", "scalar", "failures", "extracted")
    assert all(left[key] == right[key] for key in keys), (left, right)

fixtures = sorted(pathlib.Path(args.fixtures).glob("*.zip"))
for path in fixtures:
    old, new = [run(exe, [path], extract=True, check=True) for exe in (args.reference, args.candidate)]
    same(old, new)
    if path.name in ("standard.zip", "zip64.zip", "descriptor.zip", "sfx.zip"):
        assert not new["failures"], (path, new)
    if path.name == "bad-crc.zip":
        assert new["failures"] > 0, (path, new)
print(json.dumps({"fixture_compatibility": len(fixtures), "scalar": new["scalar"]}))
archives = []
for pattern in args.archives:
    archives.extend(sorted(glob.glob(pattern)))
if archives:
    if args.extract_installed:
        old, new = [run(exe, archives, extract=True, check=True) for exe in (args.reference, args.candidate)]
        same(old, new)
        assert not new["failures"], new
        print(json.dumps({"installed_extraction": new}))
    samples = {"reference": [], "candidate": []}
    for iteration in range(args.runs + 1):
        order = ("reference", "candidate") if iteration % 2 == 0 else ("candidate", "reference")
        pair = {name: run(getattr(args, name), archives) for name in order}
        same(pair["reference"], pair["candidate"])
        assert not pair["candidate"]["failures"], pair
        if iteration:
            for name in samples:
                samples[name].append(pair[name]["ms"])
    print(json.dumps({"samples_ms": samples, "median_ms": {name: statistics.median(values) for name, values in samples.items()},
                      "last": pair}, indent=2))
