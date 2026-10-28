#!/usr/bin/env python3
"""ARMv8-A execution and independently checked AAPCS64 interoperation."""
import argparse
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
CC='aarch64-linux-gnu-gcc-11'

def run(args,input_data='',timeout=90):
    return subprocess.run([str(x) for x in args],input=input_data,capture_output=True,text=True,timeout=timeout)
def outcome(result):
    if result.returncode<0:raise RuntimeError('signal '+str(-result.returncode)+' '+result.stderr)
    return result.stdout,result.returncode

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--compiler',required=True);args=parser.parse_args()
    compiler=str(Path(args.compiler).resolve());checks=failures=0
    def check(ok,label,detail=''):
        nonlocal checks,failures
        checks+=1;failures+=not ok
        print(('PASS ' if ok else 'FAIL ')+label,flush=True)
        if not ok:print(detail,flush=True)
    def required(result):
        if result.returncode:raise RuntimeError(result.stderr+result.stdout)
        return result
    with tempfile.TemporaryDirectory() as temp:
        d=Path(temp)
        def compile(source,opt):
            sy=d/'test.sy';sy.write_text(source);assembly=d/'target.s'
            # Isolate the ABI from inlining while retaining O1 allocation.
            options=['--passes=mem2reg'] if opt else []
            required(run([compiler,sy,f'-O{opt}','-S','-o',assembly,*options]));return assembly
        def execute(assembly,*extra):
            binary=d/'target';required(run([CC,'-march=armv8-a','-static',assembly,*extra,'-lm','-o',binary]));return outcome(run(['qemu-aarch64','-cpu','cortex-a53',binary]))
        def reference(source):
            path=d/'ref.c';path.write_text(source);binary=d/'native'
            required(run(['gcc','-w','-ffp-contract=off','-include',ROOT/'runtime/sylib.h',path,ROOT/'runtime/sylib.c','-lm','-o',binary]));return outcome(run([binary]))
        samples={
            'variadic_20_fp':'int main(){putf("'+(' %f'*20)+'",'+','.join(f'{k}.25' for k in range(20))+');return 0;}',
            'variadic_mixed':'int main(){putf("'+(' %d %f'*14)+'",'+','.join(x for k in range(14) for x in (str(k),f'{k}.5'))+');return 0;}',
        }
        for kind in ('int','float'):
            n=1000;params=','.join(f'{kind} p{k}' for k in range(n));values=','.join(str(k+1)+('.25' if kind=='float' else '') for k in range(n))
            samples['large_stack_'+kind]=f'{kind} f({params}){{return p999;}}int main(){{'+('putfloat' if kind=='float' else 'putint')+f'(f({values}));return 0;}}'
        for name,source in samples.items():
            try:
                expected=reference(source)
                for opt in (0,1):
                    actual=execute(compile(source,opt),ROOT/'runtime/sylib.c');check(actual==expected,f'{name} O{opt}',str((expected,actual)))
            except Exception as e:check(False,name,str(e))
        params=','.join(x for k in range(20) for x in (f'int a{k}',f'float b{k}'))
        values=','.join(x for k in range(20) for x in (str(k+1),f'{k}.25'))
        expression='+'.join(x for k in range(20) for x in (f'a{k}*{k+1}',f'b{k}*{k+2}'))
        definition=f'float probe({params}){{return {expression};}}'
        source=definition+f'int main(){{putfloat(probe({values}));return 0;}}'
        expected=reference(source)
        (d/'caller.c').write_text('#include <stdio.h>\n'+f'float probe({params});int main(){{printf("%a",probe({values}));return 0;}}')
        (d/'callee.c').write_text(definition)
        for opt in (0,1):
            try:
                text=compile(source,opt).read_text();prefix,marker,suffix=text.partition('  .globl main\n');check(bool(marker),f'interoperation split O{opt}')
                (d/'callee.s').write_text(prefix);(d/'caller.s').write_text('  .text\n'+marker+suffix)
                actual=execute(d/'callee.s',d/'caller.c');check(actual==expected,f'GCC caller -> SysY 40 mixed parameters O{opt}',str((expected,actual)))
                actual=execute(d/'caller.s',d/'callee.c',ROOT/'runtime/sylib.c');check(actual==expected,f'SysY caller -> GCC 40 mixed parameters O{opt}',str((expected,actual)))
            except Exception as e:check(False,'interoperation O'+str(opt),str(e))
        # The assembly driver preserves its own caller's registers, seeds all
        # callee-saved GPRs and low 64 FP bits, then checks them after SysY code.
        source='float compute(int n,float x){'+''.join(f'int a{k}=n+{k+1};float b{k}=x+{k}.25;' for k in range(20))+'putch(65);return '+'+'.join(x for k in range(20) for x in (f'a{k}',f'b{k}'))+';}int main(){return 0;}'
        driver=['.text','.global check_preserved','.type check_preserved,%function','check_preserved:','stp x29,x30,[sp,#-160]!','mov x29,sp']
        for k in range(19,29,2):driver.append(f'stp x{k},x{k+1},[sp,#{16+(k-19)*8}]')
        for k in range(8,16,2):driver.append(f'stp d{k},d{k+1},[sp,#{96+(k-8)*8}]')
        for k in range(19,29):driver.append(f'mov x{k},#{k}')
        for k in range(8,16):driver += [f'mov x9,#{k}',f'fmov d{k},x9']
        driver += ['mov w0,#3','movz w9,#0x3fa0,lsl #16','fmov s0,w9','bl compute','mov x9,sp','and x9,x9,#15','cbnz x9,.Lbad']
        for k in range(19,29):driver += [f'cmp x{k},#{k}','b.ne .Lbad']
        for k in range(8,16):driver += [f'fmov x9,d{k}',f'cmp x9,#{k}','b.ne .Lbad']
        driver += ['mov w0,#0','b .Ldone','.Lbad:','mov w0,#1','.Ldone:']
        for k in range(19,29,2):driver.append(f'ldp x{k},x{k+1},[sp,#{16+(k-19)*8}]')
        for k in range(8,16,2):driver.append(f'ldp d{k},d{k+1},[sp,#{96+(k-8)*8}]')
        driver += ['ldp x29,x30,[sp],#160','ret','.section .note.GNU-stack,"",%progbits']
        (d/'driver.s').write_text('\n'.join(driver)+'\n');(d/'driver.c').write_text('int check_preserved(void);int main(){return check_preserved();}')
        for opt in (0,1):
            try:
                text=compile(source,opt).read_text();prefix,marker,suffix=text.partition('  .globl main\n');required_marker=bool(marker)
                if not required_marker:raise RuntimeError('missing main boundary')
                (d/'compute.s').write_text(prefix);actual=execute(d/'compute.s',d/'driver.s',d/'driver.c',ROOT/'runtime/sylib.c')
                check(actual==('A',0),f'x19-x28/d8-d15 preserved and SP aligned O{opt}',str(actual))
            except Exception as e:check(False,'callee saves O'+str(opt),str(e))
    print(f'RESULTS: {checks} checks, {failures} failures',flush=True);return bool(failures)
if __name__=='__main__':raise SystemExit(main())
