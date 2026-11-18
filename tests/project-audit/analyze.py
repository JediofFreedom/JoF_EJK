"""Run reproducible first-party clang analyzer checks from a production database.

Usage: python analyze.py --database BUILD/compile_commands.json --output OUT
       --clang-tidy /path/to/clang-tidy [--jobs 4]
Output records errors separately; a translation unit that fails to parse is
never reported as clean. Run outside timed game benchmarks.
"""
import argparse
import concurrent.futures
import hashlib
import json
import pathlib
import re
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--database', type=pathlib.Path, required=True)
p.add_argument('--output', type=pathlib.Path, required=True)
p.add_argument('--clang-tidy', default='clang-tidy')
p.add_argument('--jobs', type=int, default=4)
p.add_argument('--path', action='append', default=[], help='Only analyze these repository path prefixes')
args = p.parse_args()
root = pathlib.Path(__file__).resolve().parents[2]
args.output.mkdir(parents=True, exist_ok=True)
commands = json.loads(args.database.read_text(encoding='utf-8-sig'))
units = {}
for item in commands:
    file = pathlib.Path(item['file']).resolve()
    try:
        relative = file.relative_to(root).as_posix()
    except ValueError:
        continue
    if not relative.startswith(('codemp/', 'shared/')) or '/vulkan/' in relative:
        continue
    if relative == 'codemp/game/sqlite3.c':
        continue  # Vendored SQLite amalgamation; not first-party engine/game code.
    if args.path and not any(relative.startswith(prefix) for prefix in args.path):
        continue
    # Preserve every distinct compiler context (shared game code has several).
    signature = json.dumps(item, sort_keys=True)
    units[signature] = (relative, item)

def analyze(unit):
    relative, entry = unit
    key = hashlib.sha256(json.dumps(entry, sort_keys=True).encode()).hexdigest()[:16]
    directory = args.output / key
    directory.mkdir(exist_ok=True)
    (directory / 'compile_commands.json').write_text(json.dumps([entry]))
    started = time.monotonic()
    source_hash = hashlib.sha256(pathlib.Path(entry['file']).read_bytes()).hexdigest()
    command = [args.clang_tidy, entry['file'], '-p', str(directory), '--quiet',
        '--checks=-*,clang-analyzer-core.*,clang-analyzer-unix.Malloc,clang-analyzer-cplusplus.NewDelete*']
    if relative in ('codemp/botlib/be_ai_goal.cpp', 'codemp/botlib/be_ai_weap.cpp'):
        command.append('--extra-arg=-Wno-c++11-narrowing')
    try:
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        text = result.stdout.decode('utf-8', errors='replace')
        status = 'parse-error' if result.returncode or re.search(r'\berror:', text) else 'analyzed'
    except subprocess.TimeoutExpired as error:
        text = (error.stdout or b'').decode('utf-8', errors='replace')
        status = 'timeout'
    (directory / 'diagnostics.txt').write_text(text, encoding='utf-8')
    diagnostics = [line for line in text.splitlines() if 'warning:' in line and '[clang-analyzer-' in line]
    record = dict(file=relative, context=key, source_sha256=source_hash, analyzer_args=command[1:], status=status, diagnostics=diagnostics,
                seconds=round(time.monotonic() - started, 2))
    (directory / 'result.json').write_text(json.dumps(record, indent=2))
    return record

results = []
print(f'Analyzing {len(units)} first-party compiler contexts', flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
    for result in pool.map(analyze, units.values()):
        results.append(result)
        temporary = args.output / 'results.tmp'
        temporary.write_text(json.dumps(results, indent=2))
        temporary.replace(args.output / 'results.json')
        if result['diagnostics'] or result['status'] != 'analyzed':
            print(json.dumps(result), flush=True)
summary = dict(contexts=len(results), analyzed=sum(r['status'] == 'analyzed' for r in results),
               with_diagnostics=sum(bool(r['diagnostics']) for r in results))
print(json.dumps(summary), flush=True)
(args.output / 'summary.json').write_text(json.dumps(summary, indent=2))
