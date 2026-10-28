#!/usr/bin/env python3
"""Internal execution tests with an independent native-C oracle."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
from test_official import run_test

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', default=str(ROOT / 'build/compiler'))
    parser.add_argument('--reference-compiler', default='gcc')
    parser.add_argument('--timeout', type=int, default=45)
    args = parser.parse_args()
    files = [p for d in ['samples', 'mem2reg', 'passes'] for p in sorted((ROOT/'tests'/d).glob('*.sy'))]
    if not files:
        print('Error: no test cases selected', file=sys.stderr)
        return 1
    failures = 0
    with tempfile.TemporaryDirectory() as temp:
        reference = str(Path(temp) / 'reference')
        for source in files:
            try:
                inp = source.with_suffix('.in')
                input_data = inp.read_text() if inp.exists() else ''
                built = subprocess.run([args.reference_compiler, '-w', '-include', str(ROOT/'runtime/sylib.h'), '-x', 'c', str(source), str(ROOT/'runtime/sylib.c'), '-lm', '-o', reference], capture_output=True, text=True, timeout=60)
                if built.returncode:
                    raise RuntimeError('Oracle compilation failed: ' + built.stderr)
                result = subprocess.run([reference], input=input_data, capture_output=True, text=True, timeout=args.timeout)
                if result.returncode < 0:
                    raise RuntimeError('Oracle terminated by signal')
                expected = result.stdout + ('\n' if result.stdout and not result.stdout.endswith('\n') else '') + str(result.returncode) + '\n'
                verified = subprocess.run([args.compiler, str(source), '--run-pass=mem2reg', '--verify-ir'], capture_output=True, text=True, timeout=60)
                if verified.returncode:
                    raise RuntimeError('IR verification failed: ' + verified.stderr)
                for opt in [0, 1]:
                    ok, message = run_test(args.compiler, str(source), opt_level=opt, timeout_sec=args.timeout, expected_output=expected)
                    print(f'{source.name} -O{opt}: {message}')
                    failures += not ok
            except (OSError, subprocess.TimeoutExpired, RuntimeError) as exc:
                failures += 1
                print(f'{source.name}: FAILED: {exc}')
    print(f'RESULTS: {len(files)} cases, {failures} failures')
    return 1 if failures else 0

if __name__ == '__main__':
    sys.exit(main())
