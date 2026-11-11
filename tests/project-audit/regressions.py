"""Configure, build and run every standalone test suite; fail on any failure.

Run in a compiler environment, e.g. a VS Developer Command Prompt on Windows:
  python tests/project-audit/regressions.py --output C:/build/ejk-checks
Additional CMake arguments may follow -- (e.g. -G Ninja or a NASM compiler).
"""
import argparse
import json
import pathlib
import subprocess
import sys
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=pathlib.Path, required=True)
p.add_argument('--cmake', default='cmake')
p.add_argument('--jobs', type=int, default=4)
p.add_argument('cmake_args', nargs=argparse.REMAINDER)
args = p.parse_args()
source = pathlib.Path(__file__).resolve().parents[2]
args.output = args.output.resolve()
args.output.mkdir(parents=True, exist_ok=True)
cmake_args = args.cmake_args[1:] if args.cmake_args[:1] == ['--'] else args.cmake_args
results = []
for suite in sorted((source / 'tests').iterdir()):
    if not (suite / 'CMakeLists.txt').is_file():
        continue
    build = args.output / suite.name
    started = time.monotonic()
    commands = [
        [args.cmake, '-S', str(suite), '-B', str(build), '-DCMAKE_BUILD_TYPE=Release',
         '-DCMAKE_POLICY_VERSION_MINIMUM=3.5'] + cmake_args,
        [args.cmake, '--build', str(build), '--config', 'Release', '--parallel', str(args.jobs)],
        [args.cmake, '--build', str(build), '--config', 'Release', '--target', 'test']
    ]
    # CTest exposes useful failed-test output on every supported generator.
    ctest = pathlib.Path(args.cmake).with_name('ctest' + pathlib.Path(args.cmake).suffix) if pathlib.Path(args.cmake).parent != pathlib.Path('.') else 'ctest'
    commands[-1] = [str(ctest), '--test-dir', str(build), '-C', 'Release', '--output-on-failure', '--no-tests=error']
    failed = None
    log_path = args.output / (suite.name + '.log')
    with log_path.open('w', encoding='utf-8') as log:
        for step, command in enumerate(commands):
            try:
                result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
                if result.returncode:
                    failed = ['configure', 'build', 'test'][step]
                    break
            except OSError as error:
                log.write(str(error))
                failed = ['configure', 'build', 'test'][step]
                break
    record = dict(suite=suite.name, passed=failed is None, failed_step=failed,
                  seconds=round(time.monotonic() - started, 2), log=str(log_path))
    results.append(record)
    print(json.dumps(record), flush=True)
    temporary = args.output / 'results.tmp'
    temporary.write_text(json.dumps(results, indent=2))
    temporary.replace(args.output / 'results.json')
print(f"Passed: {sum(r['passed'] for r in results)}/{len(results)}", flush=True)
sys.exit(0 if results and all(r['passed'] for r in results) else 1)
