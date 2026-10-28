#!/usr/bin/env python3
"""UB-free bounded SysY differential cases and every optional-pass prefix.
Clang -O0/-O1/-O2 must agree; check execution stdout AND normal exit status.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor,as_completed
import hashlib,json,random,re,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def command(args,**kwargs):
    r=subprocess.run([str(x) for x in args],capture_output=True,text=True,timeout=kwargs.pop('timeout',60),**kwargs)
    if any(x in r.stderr for x in ('AddressSanitizer','LeakSanitizer','runtime error:')):raise RuntimeError('Sanitizer: '+r.stderr)
    return r
def required(r,stage):
    if r.returncode:raise RuntimeError(stage+': '+r.stderr)
    return r
def outcome(r):
    if r.returncode<0:raise RuntimeError('Signal '+str(-r.returncode))
    return r.stdout,r.returncode
def cases(seed,count):
    fixed={
    'phi_swap':('int main(){int i=0;int a=2;int b=3;while(i<3){int t=a;a=b;b=t+a;i=i+1;}putint(a);putch(32);return b;}',''),
    'zero_trip_div':('int f(int n,int x){int i=0;int s=0;while(i<n){s=s+9/x;i=i+1;}return s;}int main(){return f(getint(),0);}','0\n'),
    'negative_stride':('int main(){int i=7;int s=0;while(i>0){s=s+i*3;i=i-2;}putint(s);return 0;}',''),
    'continue_break':('int f(int n){int i=0;int s=0;while(i<n){i=i+1;if(i%3==0)continue;if(i>12)break;s=s+i*5;}return s;}int main(){putint(f(getint()));return 0;}','20\n'),
    'nested_loop':('int main(){int n=getint();int i=0;int s=0;while(i<n){int j=0;while(j<4){if(j==2)s=s+i;else s=s+j*7;j=j+1;}i=i+1;}putint(s);return 0;}','7\n'),
    'inline_returns':('int f(int x){if(x<0)return -x;if(x==0)return 17;return x*3;}int main(){int x=getint();putint(f(x)+f(x+1)+f(0));return 0;}','-1\n'),
    'no_overflow_compare':('int f(int x){if(x+1>x)return 1;return 0;}int main(){putint(f(getint()));return 0;}','5\n'),
    'recursive':('int f(int n){if(n<2)return n;return f(n-1)+f(n-2);}int main(){putint(f(9));return 0;}',''),
    'call_memory':('int a[3]={2,3,4};int change(int x){a[x]=a[x]+5;return a[x];}int main(){int x=a[1];int y=change(1);putint(x+y+a[1]);return 0;}',''),
    'array_params':('int sum(int a[],int n){int i=0;int s=0;while(i<n){s=s+a[i];i=i+1;}return s;}int main(){int a[5]={1,2,3,4,5};putint(sum(a,5));return 0;}',''),
    'unroll_side_effect':('int a;int f(int x){a=a+1;return x+a;}int main(){int i=0;int s=0;while(i<4){s=s+f(i);i=i+1;}putint(a);putch(32);putint(s);return 0;}',''),
    'float_memory':('float a[3]={1.5,2.25,-0.5};float f(float x){a[1]=x+0.25;return a[1];}int main(){float x=a[1];float y=f(getfloat());putfloat(x+y+a[1]);return 0;}','3.5\n'),
    'conditional_loop_entry':('int f(int x,int n){int i=0;int s=x;if(n<0){n=3;s=x+1;}while(i<n){s=s+x*3;i=i+1;}return s;}int main(){putint(f(3,getint()));return 0;}','-2\n')}
    parameters=[f'int x{i}' for i in range(20)]+[f'float f{i}' for i in range(16)]
    terms=[f'x{i}' for i in range(20)]+[f'f{i}' for i in range(16)]
    fixed['register_pressure']=('float f('+','.join(parameters)+'){putch(65);return '+'+'.join(terms)+';}int main(){float r=f('+','.join([str(i) for i in range(20)]+[f'{i}.5' for i in range(16)])+');putfloat(r);return 0;}','')
    rng=random.Random(seed)
    for k in range(count):
        c=[rng.randrange(1,10) for _ in range(8)];n=rng.randrange(0,13);step=rng.choice([1,2,3]);inp=rng.randrange(-20,21)
        source=f"""int a[12]={{1,2,3,4,5,6,7,8,9,10,11,12}};
int small(int x){{if(x<0)return -x+{c[0]};return x*{c[1]}+{c[2]};}}
int work(int x,int n){{int i=0;int s=x;int u=2;int v=3;float f=0.5;
while(i<n){{int t=u;u=v;v=(t+u)%997;
if(i%3==0)s=(s+small(x+i))%10007;else s=(s+a[i%12]*{c[3]})%10007;
a[i%12]=(a[i%12]+{c[4]})%103;
f=f*0.5+{c[5]}.25;i=i+{step};}}
putfloat(f);putch(32);return s+u+v;}}
int main(){{int x=getint();int s=work(x,{n});putint(s);putch(32);putint(a[{c[6]}]);return s%127;}}
"""
        fixed[f'random_{k:03}']=(source,f'{inp}\n')
    return fixed

def main():
    p=argparse.ArgumentParser();p.add_argument('--compiler',required=True);p.add_argument('--reference',default='clang-18')
    p.add_argument('--runtime-library');p.add_argument('--random-cases',type=int,default=32);p.add_argument('--seed',type=int,default=20261028)
    p.add_argument('--jobs',type=int,default=4);p.add_argument('--skip-bisect',action='store_true');p.add_argument('--report',type=Path)
    args=p.parse_args();compiler=str(Path(args.compiler).resolve());samples=cases(args.seed,args.random_cases);results=[]
    def worker(name,sample):
        source,input_data=sample;checks=0
        with tempfile.TemporaryDirectory(prefix='sysy-middle-') as temp:
            d=Path(temp);sy=d/(name+'.sy');sy.write_text(source);references=[]
            for opt in (0,1,2):
                native=d/f'clang{opt}'
                required(command([args.reference,f'-O{opt}','-ffp-contract=off','-w','-include',ROOT/'runtime/sylib.h','-x','c',sy,ROOT/'runtime/sylib.c','-lm','-o',native]),'Clang build')
                references.append(outcome(command([native],input=input_data,timeout=10)));checks+=1
            if len(set(references))!=1:raise RuntimeError('Clang -O0/-O1/-O2 disagree: '+str(references))
            expected=references[0]
            def execute(options):
                asm=d/'out.s';binary=d/'target'
                required(command([compiler,sy,'-S','-o',asm,*options]),'SysY compile')
                required(command(['aarch64-linux-gnu-gcc-11','-march=armv8-a','-static',asm,args.runtime_library or ROOT/'runtime/sylib.c','-lm','-o',binary]),'ARM64 link')
                actual=outcome(command(['qemu-aarch64',binary],input=input_data,timeout=10))
                if actual!=expected:raise RuntimeError('Mismatch '+str(options)+' expected='+str(expected)+' actual='+str(actual))
            for opt in ('-O0','-O1'):execute([opt]);checks+=1
            if not args.skip_bisect and name in ('phi_swap','inline_returns','continue_break'):
                trace=required(command([compiler,sy,'-O1','--trace-passes','--emit-mir']),'trace')
                numbers=[int(x) for x in re.findall(r'^PASS (\d+) ',trace.stderr,re.M)]
                for limit in range(max(numbers,default=0)+1):execute(['-O1','--opt-bisect-limit='+str(limit)]);checks+=1
            if name=='phi_swap':
                for level in ('hir','mir','lir'):
                    dump=required(command([compiler,sy,'-O1','--emit-'+level]),level+' dump')
                    if 'level='+level.upper() not in dump.stdout:raise RuntimeError('Dump level missing')
                    required(command(['llvm-as-18','-o','/dev/null'],input=dump.stdout),'LLVM parse');checks+=1
            if name in ('random_000','no_overflow_compare'):execute(['-O1','--assume-signed-no-overflow']);checks+=1
        return {'case':name,'checks':checks,'status':'passed','source_sha256':hashlib.sha256(source.encode()).hexdigest()}
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        jobs={pool.submit(worker,name,sample):name for name,sample in samples.items()}
        for job in as_completed(jobs):
            name=jobs[job]
            try:item=job.result()
            except Exception as exc:item={'case':name,'status':'failed','checks':0,'detail':str(exc)}
            results.append(item);print(item,flush=True)
    failures=sum(r['status']!='passed' for r in results)
    report={'compiler_sha256':hashlib.sha256(Path(compiler).read_bytes()).hexdigest(),'seed':args.seed,'random_cases':args.random_cases,'cases':len(results),'checks':sum(r['checks'] for r in results),'failures':failures,'results':sorted(results,key=lambda r:r['case'])}
    if args.report:args.report.write_text(json.dumps(report,indent=2))
    print('RESULTS:',report['cases'],'cases,',report['checks'],'checks,',failures,'failures',flush=True);return bool(failures)
if __name__=='__main__':raise SystemExit(main())
