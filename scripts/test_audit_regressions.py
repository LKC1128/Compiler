#!/usr/bin/env python3
"""Reproductions for CODEX_AUDIT, checked against independent C execution."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch
import test_official

ROOT=Path(__file__).resolve().parents[1]

def command(args, input_data=None, timeout=60):
    result = subprocess.run([str(x) for x in args], input=input_data, capture_output=True, text=True, timeout=timeout)
    if any(marker in result.stderr for marker in ('AddressSanitizer', 'LeakSanitizer', 'runtime error:')):
        raise RuntimeError('Sanitizer failure: '+result.stderr)
    return result

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--compiler', required=True)
    args=parser.parse_args()
    compiler=str(Path(args.compiler).resolve())
    failures=[];checks=0
    def check(ok, label, detail=''):
        nonlocal checks
        checks+=1
        if not ok: failures.append(label+': '+detail)
        print(('PASS ' if ok else 'FAIL ')+label,flush=True)
    cases={
        'runtime_init':'int main(){int x=7;int a[2]={x};return a[0];}',
        'runtime_init_effect':'int f(){putint(9);return 7;}int main(){int a[2]={f()};return a[0];}',
        'const_float_div':'const float a=1/2;int main(){putfloat(a);return 0;}',
        'const_int_sum':'const int a=1.5+1.5;int main(){return a;}',
        'const_cross_type':'const int a=2;const float b=a;int main(){putfloat(b);return 0;}',
        'const_array_cross_type':'const int a[2]={7,9};const float b=a[1];int main(){putfloat(b);return 0;}',
        'global_div':'float a=1/2;int main(){putfloat(a);return 0;}',
        'local_const_div':'int main(){const float a=1/2;putfloat(a);return 0;}',
        'dead_div':'int main(){if(0){int a=-2147483647-1;return a/-1;}return 0;}',
        'negative_zero':'float neg(float x){return -x;}int main(){putfloat(neg(0.0));return 0;}',
        'zero_array':'int main(){float a[1]={-0.0};putfloat(a[0]);return 0;}',
        'zero_large_array':'int main(){float a[65]={-0.0,1.0};putfloat(a[0]);return 0;}',
        'frame2048_live':'int main(){int a[502];a[0]=1;return a[0];}',
        'frame2048':'int main(){int a[508];return 0;}',
        'phi_swap':'int main(){int a=1;int b=2;int i=0;while(i<3){int t=a;a=b;b=t;i=i+1;}return a*10+b;}',
        'phi_float':'int main(){float a=1.5;float b=2.5;int i=0;while(i<3){float t=a;a=b;b=t;i=i+1;}putfloat(a*10+b);return 0;}',
        'memory_alias':'int f(int a[],int b[]){a[0]=3;int x=b[0];a[0]=7;return x*10+b[0];}int main(){int a[1]={1};return f(a,a);}',
        'putf_mixed':r'int main(){putf("%d %f %c\n",7,1.25,65);return 0;}',
        'putf_escapes':r'int main(){putf("a\t\\\"\101\x42\n");return 0;}',
        'putf_stack':r'int main(){putf("%f %f %f %f %f %f %f %f %f %f\n",1.0,2.0,3.0,4.0,5.0,6.0,7.0,8.0,9.0,10.0);return 0;}',
    }
    for ty in ['int', 'float']:
        params=','.join(ty+' p'+str(i) for i in range(300))
        values=','.join(str(i)+('.0' if ty=='float' else '') for i in range(300))
        output='putfloat' if ty=='float' else 'putint'
        cases['args300_'+ty]=ty+' f('+params+'){return p299;}int main(){'+output+'(f('+values+'));return 0;}'
    bad={
        'undeclared':'int main(){return x+1;}',
        'arity':'int f(int x){return x;}int main(){return f();}',
        'const_write':'const int x=1;int main(){x=2;return x;}',
        'local_const_write':'int main(){const int x=1;x=2;return x;}',
        'variable_dimension':'int main(){int n=2;int a[n];return 0;}',
        'duplicate':'int main(){int a;int a;return 0;}',
        'duplicate_main':'int main(){return 0;}int main(){return 1;}',
        'missing_main':'int f(){return 0;}',
        'octal':'int main(){return 09;}',
        'float_token':'int main(){float a=1e;return 0;}',
        'comment':'int main(){return 0;} /*',
        'string':'int main(){putf("abc);return 0;}',
        'void_return':'void f(){return 1;}int main(){f();return 0;}',
        'return_void':'int main(){return;}',
        'tensor_undeclared':'int main(){tensor int a[2];a=b;return 0;}',
        'tensor_shape':'int main(){tensor int a[2][3];tensor int b[2][2];tensor int c[2][2];c=a@b;return 0;}',
    }
    with tempfile.TemporaryDirectory() as temp:
        temp=Path(temp)
        for name,source in cases.items():
            sy=temp/(name+'.sy');sy.write_text(source)
            native=temp/'native'
            build=command(['gcc','-w','-include',ROOT/'runtime/sylib.h','-x','c',sy,ROOT/'runtime/sylib.c','-o',native,'-lm'])
            if build.returncode:
                check(False,name+' oracle',build.stderr);continue
            reference=command([native])
            expected=reference.stdout+('\n' if reference.stdout and not reference.stdout.endswith('\n') else '')+str(reference.returncode)+'\n'
            for opt in [0,1]:
                ok,detail=test_official.run_test(compiler,str(sy),opt_level=opt,expected_output=expected)
                check(ok,f'{name} -O{opt}',detail)
        for name,source in bad.items():
            sy=temp/(name+'.sy');sy.write_text(source)
            for opt in [0,1]:
                result=command([compiler,sy,f'-O{opt}','-S','-o',temp/'bad.s'])
                check(result.returncode>0 and bool(result.stderr.strip()),f'reject {name} -O{opt}',result.stderr)
        sy=temp/'valid.sy';sy.write_text('int main(){return 0;}')
        result=command([compiler,sy,'-S','-o',temp/'absent'/'out.s'])
        check(result.returncode>0 and bool(result.stderr.strip()),'output failure reported')
        # Standard-toolchain interoperability must work in both directions.
        params=','.join('float p'+str(i) for i in range(9))
        values=','.join(str(i)+'.0' for i in range(1,10))
        sy.write_text('float f('+params+'){return p8;}int main(){putfloat(f('+values+'));return 0;}')
        gcc_caller=temp/'caller.c';gcc_callee=temp/'callee.c'
        gcc_caller.write_text('#include <stdio.h>\nfloat f('+params+');int main(){printf("%a",f('+values+'));return 0;}')
        gcc_callee.write_text('float f('+params+'){return p8;}')
        for opt in [0,1]:
            assembly=temp/'abi.s';result=command([compiler,sy,f'-O{opt}','-S','-o',assembly])
            if result.returncode:
                check(False,f'ABI compile -O{opt}',result.stderr);continue
            prefix,marker,suffix=assembly.read_text().partition('  .globl main\n')
            callee=temp/'callee.s';callee.write_text(prefix)
            caller=temp/'caller.s';caller.write_text('  .text\n'+marker+suffix)
            for label,files in [('gcc-to-sysy',[gcc_caller,callee]),('sysy-to-gcc',[caller,gcc_callee,ROOT/'runtime/sylib.c'])]:
                executable=temp/'abi'
                built=command(['aarch64-linux-gnu-gcc-11','-march=armv8-a',*files,'-o',executable])
                result=command(['qemu-aarch64','-L','/usr/aarch64-linux-gnu',executable]) if built.returncode==0 else built
                check(result.returncode==0 and result.stdout=='0x1.2p+3',f'ABI {label} -O{opt}',result.stdout+result.stderr)
        # Fault injection: execution signals and timeouts must never become success.
        sy.with_suffix('.out').write_text('245\n')
        def signalled(cmd,**kwargs):
            return subprocess.CompletedProcess(cmd,-11 if cmd[0]=='qemu-aarch64' else 0,'','')
        with patch.object(test_official.subprocess,'run',signalled):
            check(not test_official.run_test(compiler,str(sy))[0],'runner rejects signal, not masked exit 245')
        def timed_out(cmd,**kwargs):
            if cmd[0]=='qemu-aarch64': raise subprocess.TimeoutExpired(cmd,1)
            return subprocess.CompletedProcess(cmd,0,'','')
        with patch.object(test_official.subprocess,'run',timed_out):
            check(not test_official.run_test(compiler,str(sy))[0],'runner rejects timeout')
        sy.with_suffix('.out').unlink()
        with patch.object(test_official.subprocess,'run',signalled) as mocked:
            check(not test_official.run_test(compiler,str(sy))[0],'runner rejects missing oracle')
        empty=temp/'empty';empty.mkdir()
        result=command([sys.executable,ROOT/'scripts/test_official.py','--dir',empty,'--compiler',compiler])
        check(result.returncode>0,'runner rejects empty selection')
        result=command([sys.executable,ROOT/'scripts/test_official.py','--dir',ROOT/'tests/official/functional','--filter','00_main','--compiler',temp/'missing'])
        check(result.returncode>0,'runner CLI fails when compiler is missing')
    print(f'RESULTS: {checks} checks, {len(failures)} failures',flush=True)
    for failure in failures: print(failure, file=sys.stderr)
    return bool(failures)

if __name__=='__main__':
    sys.exit(main())
