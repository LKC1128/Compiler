#!/usr/bin/env python3
"""Codex audit regressions: diagnostics, dead code, ARM64 execution and PC-relative."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from test_official import run_test


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--compiler',required=True)
    parser.add_argument('--ir-test',help='CMake compliance_regression executable')
    parser.add_argument('--runtime-library')
    args=parser.parse_args()
    compiler=str(Path(args.compiler).resolve())
    checks=failures=0
    def check(ok,label,detail=''):
        nonlocal checks,failures
        checks+=1;failures+=not ok
        print(('PASS ' if ok else 'FAIL ')+label,flush=True)
        if not ok:print(detail,flush=True)
    def run(command):
        return subprocess.run([str(x) for x in command],capture_output=True,text=True,timeout=60)
    invalid=[
        ('dead_name','int main(){\n  return 0;\n  missing=1;\n}','line 3, col 3'),
        ('live_name','int main(){\n  missing=1;\n  return 0;\n}','line 2, col 3'),
        ('dead_break','int main(){\n  return 0;\n  break;\n}','line 3, col 3'),
        ('dead_continue','int main(){\n  return 0;\n  continue;\n}','line 3, col 3'),
        ('dead_decl','int main(){return 0; int x; int x;}','Duplicate declaration'),
        ('after_break','int main(){while(1){break; missing=1;}return 0;}','Undeclared identifier'),
        ('after_continue','int main(){while(0){continue;missing=1;}return 0;}','Undeclared identifier'),
        ('dead_return','int main(){return 0;return;}','Non-void function must return a value'),
        ('dead_tensor','int main(){return 0;tensor int a[2];tensor int b[3];a=a+b;}','Tensor shape mismatch'),
    ]
    positive=[
        ('dead_large_array','int main(){putint(7);return 0;int a[100000000];}','7\n0\n'),
        ('dead_side_effect','int main(){putint(7);return 3;putint(99);int a[2]={1,2};a[0]=9;}','7\n3\n'),
        ('dead_loop_tail','int main(){int i=0;while(i<4){i=i+1;if(i<3){continue;putint(99);}break;putint(88);}putint(i);return 0;}','3\n0\n'),
        ('dead_tensor_value','int main(){tensor int a[2]={2,3};putarray(2,a);return 0;tensor int b[2]={7,8};a=a+b;}','2: 2 3\n0\n'),
        ('dead_nested','int f(int x){if(x){return 7;int a=4;}return 9;while(1){putint(3);}}int main(){putint(f(1));putch(10);return f(0);}','7\n9\n'),
    ]
    with tempfile.TemporaryDirectory() as directory:
        d=Path(directory)
        for name,source,diagnostic in invalid:
            p=d/(name+'.sy');p.write_text(source)
            for opt in (0,1):
                result=run([compiler,p,f'-O{opt}','--emit-ir'])
                check(result.returncode>0 and diagnostic in result.stderr and 'line 0' not in result.stderr and
                      not any(x in result.stderr for x in ('AddressSanitizer','runtime error:','LeakSanitizer')),
                      f'reject {name} O{opt}',result.stderr)
        for name,source,expected in positive:
            p=d/(name+'.sy');p.write_text(source)
            for opt in (0,1):
                ok,detail=run_test(compiler,str(p),opt_level=opt,expected_output=expected,runtime_library=args.runtime_library)
                check(ok,f'execute {name} O{opt}',detail)
        if args.ir_test:
            result=run([args.ir_test]);check(result.returncode==0,'IR edge/self-phi/narrow compare',result.stdout+result.stderr)
            ir=run([args.ir_test,'edge-ir'])
            check(ir.returncode==0,'duplicate edge IR generation',ir.stderr)
            if ir.returncode==0:
                p=d/'edge.ll';p.write_text(ir.stdout)
                parsed=run(['llvm-as-18',p,'-o','/dev/null'])
                check(parsed.returncode==0,'duplicate edge Phi LLVM verification',parsed.stderr)
            asm=run([args.ir_test,'asm']);check(asm.returncode==0,'narrow compare assembly generation',asm.stderr)
            if asm.returncode==0:
                p=d/'narrow.s';p.write_text(asm.stdout)
                link=run(['aarch64-linux-gnu-gcc-11','-static','-march=armv8-a',p,'-o',d/'narrow'])
                check(link.returncode==0,'narrow compare link',link.stderr)
                if link.returncode==0:
                    result=run(['qemu-aarch64',d/'narrow'])
                    check(result.returncode==1 and result.stdout=='','signed i8 comparison execution',result.stdout+result.stderr)
        p=d/'PC-relative.sy';p.write_text('int x=7;int f(){return x+1;}int main(){return f();}')
        compiled=run([compiler,p,'-S','-o',d/'PC-relative.s'])
        check(compiled.returncode==0,'PC-relative compile',compiled.stderr)
        if compiled.returncode==0:
            assembled=run(['aarch64-linux-gnu-gcc-11','-c','-march=armv8-a',d/'PC-relative.s','-o',d/'PC-relative.o'])
            check(assembled.returncode==0,'PC-relative assemble',assembled.stderr)
            if assembled.returncode==0:
                reloc=run(['aarch64-linux-gnu-readelf','-rW',d/'PC-relative.o'])
                check(reloc.returncode==0 and 'R_AARCH64_ADR_PREL_PG_HI21' in reloc.stdout and 'R_AARCH64_ABS64' not in reloc.stdout,'PC-relative global address relocations',reloc.stdout+reloc.stderr)
                (d/'start.s').write_text('.text\n.globl _start\n_start:\n bl main\n mov x8, #93\n svc #0\n')
                start=run(['aarch64-linux-gnu-as','-march=armv8-a',d/'start.s','-o',d/'start.o'])
                check(start.returncode==0,'high-address test entry',start.stderr)
                if start.returncode==0:
                    link=run(['aarch64-linux-gnu-ld','--no-relax','-Ttext=0x100000000','-e','_start',d/'start.o',d/'PC-relative.o','-o',d/'high'])
                    check(link.returncode==0,'PC-relative link above 4 GiB',link.stderr)
                    if link.returncode==0:
                        result=run(['qemu-aarch64',d/'high'])
                        check(result.returncode==8 and result.stdout=='','PC-relative execute above 4 GiB',result.stdout+result.stderr)
    print(f'RESULTS: {checks} checks, {failures} failures',flush=True)
    return bool(failures)

if __name__=='__main__':raise SystemExit(main())
