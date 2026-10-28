#!/usr/bin/env python3
"""Contest ABI and Tensor correctness: fixed oracles + independent matrix results."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from test_official import run_test

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--compiler',required=True)
    parser.add_argument('--runtime-library')
    parser.add_argument('--llvm-as',default='llvm-as-18')
    args=parser.parse_args()
    compiler=str(Path(args.compiler).resolve())
    checks=failures=0
    def check(ok,label,detail=''):
        nonlocal checks,failures
        checks+=1;failures+=not ok
        print(('PASS ' if ok else 'FAIL ')+label,flush=True)
        if not ok and detail:print(detail,flush=True)
    for source in sorted((ROOT/'tests/contest').glob('*.sy')):
        for opt in (0,1):
            ok,detail=run_test(compiler,str(source),opt_level=opt,expected_output=source.with_suffix('.out').read_text(),runtime_library=args.runtime_library)
            check(ok,f'{source.stem} -O{opt}',detail)
            # This is an additional parser check, never an execution oracle.
            if shutil.which(args.llvm_as):
                result=subprocess.run([compiler,str(source),f'-O{opt}','--emit-ir'],capture_output=True,text=True,timeout=60)
                parsed=subprocess.run([args.llvm_as,'-o','/dev/null'],input=result.stdout,capture_output=True,text=True,timeout=60)
                check(result.returncode==0 and parsed.returncode==0,f'{source.stem} LLVM parse -O{opt}',result.stderr+parsed.stderr)
    for source in sorted((ROOT/'tests/contest/invalid').glob('*.sy')):
        result=subprocess.run([compiler,str(source),'-O1','--emit-ir'],capture_output=True,text=True,timeout=60)
        check(result.returncode>0 and bool(result.stderr.strip()) and not any(marker in result.stderr for marker in ('AddressSanitizer','LeakSanitizer','runtime error:')),'reject '+source.stem,result.stderr)
    with tempfile.TemporaryDirectory() as directory:
        directory=Path(directory);assembly=directory/'timer.s';binary=directory/'timer'
        source=ROOT/'tests/contest/timing_abi.sysy'
        compiled=subprocess.run([compiler,'-S','-O1','-o',str(assembly),str(source)],capture_output=True,text=True,timeout=60)
        linked=subprocess.run(['aarch64-linux-gnu-gcc-11','-static','-march=armv8-a',str(assembly),args.runtime_library or str(ROOT/'runtime/sylib.c'),'-o',str(binary),'-lm'],capture_output=True,text=True,timeout=60)
        executed=subprocess.run(['qemu-aarch64',str(binary)],capture_output=True,text=True,timeout=30) if linked.returncode==0 else linked
        check(compiled.returncode==0 and linked.returncode==0 and executed.returncode==0 and executed.stdout=='7\n' and '@0002-0004' in executed.stderr and 'TOTAL:' in executed.stderr,'official CLI, .sysy, static timing ABI and stderr',compiled.stderr+linked.stderr+executed.stdout+executed.stderr)
        if linked.returncode==0:
            headers=subprocess.run(['aarch64-linux-gnu-readelf','-l',str(binary)],capture_output=True,text=True)
            check(headers.returncode==0 and 'INTERP' not in headers.stdout,'generated binary is statically linked',headers.stdout)
    print(f'RESULTS: {checks} checks, {failures} failures',flush=True)
    return bool(failures)

if __name__=='__main__':
    raise SystemExit(main())
