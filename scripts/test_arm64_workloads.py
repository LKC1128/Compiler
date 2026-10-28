#!/usr/bin/env python3
"""Independent bounded-input checks of expensive, unmodified ARM sources.
These are additional tests; they do not replace official input/output files.
"""
import argparse
from pathlib import Path
import random
import subprocess
import tempfile
from test_official import run_test
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--compiler',required=True);p.add_argument('--runtime-library');args=p.parse_args()
    checks=failures=0
    with tempfile.TemporaryDirectory() as temp:
        d=Path(temp)
        for source in sorted((ROOT/'tests/official/performance').glob('many_mat_cal-*.sy')):
            native=d/'native'
            reference=subprocess.run(['gcc','-O0','-fwrapv','-w','-include',str(ROOT/'runtime/sylib.h'),'-x','c',str(source),str(ROOT/'runtime/sylib.c'),'-lm','-o',str(native)],capture_output=True,text=True,timeout=60)
            if reference.returncode:raise RuntimeError(reference.stderr)
            for seed,n,repeats in ((31,2,1),(73,4,5),(109,8,17),(307,16,31)):
                rng=random.Random(seed)
                text=f'{n} {repeats}\n'+''.join(str(n)+' '+' '.join(str(rng.randrange(-2,3)) for _ in range(n))+'\n' for _ in range(n))
                expected=subprocess.run([str(native)],input=text,capture_output=True,text=True,timeout=30)
                if expected.returncode<0:raise RuntimeError('reference signal')
                sy=d/source.name;sy.write_bytes(source.read_bytes());sy.with_suffix('.in').write_text(text)
                oracle=expected.stdout+('' if not expected.stdout or expected.stdout.endswith('\n') else '\n')+str(expected.returncode)+'\n'
                for opt in (0,1):
                    ok,detail=run_test(str(Path(args.compiler).resolve()),str(sy),opt_level=opt,expected_output=oracle,runtime_library=args.runtime_library)
                    checks+=1;failures+=not ok;print(('PASS ' if ok else 'FAIL ')+f'{source.name} independent n={n} r={repeats} O{opt}',flush=True)
                    if not ok:print(detail,flush=True)
    print(f'RESULTS: {checks} checks, {failures} failures',flush=True);return bool(failures)
if __name__=='__main__':raise SystemExit(main())
