#!/usr/bin/env python3
"""Run all selected ARM performance programs; timeouts are failures."""
import argparse
from concurrent.futures import ThreadPoolExecutor,as_completed
import hashlib
import json
from pathlib import Path
from test_official import run_test
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--compiler',required=True);p.add_argument('--runtime-library');p.add_argument('--timeout',type=int,default=120);p.add_argument('--jobs',type=int,default=4);p.add_argument('--report',type=Path);p.add_argument('--filter',default='');args=p.parse_args()
    if args.timeout<=0 or args.jobs<=0:p.error('timeout and jobs must be positive')
    compiler=Path(args.compiler).resolve();digest=hashlib.sha256(compiler.read_bytes()).hexdigest()
    sources=sorted(x for x in (ROOT/'tests/official/performance').glob('*.sy') if args.filter in x.name)
    if not sources:p.error('no cases selected')
    def worker(source):
        try:
            ok,detail=run_test(str(compiler),str(source),opt_level=1,timeout_sec=args.timeout,runtime_library=args.runtime_library)
            return {'case':source.name,'status':'passed' if ok else 'timeout' if detail.startswith('Runtime timeout') else 'failed','detail':detail}
        except Exception as e:return {'case':source.name,'status':'failed','detail':str(e)}
    results=[]
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for future in as_completed([pool.submit(worker,source) for source in sources]):
            result=future.result();results.append(result);print(result['case'],result['status'],flush=True)
    summary={k:sum(x['status']==k for x in results) for k in ('passed','timeout','failed')};summary['total']=len(results)
    stable=hashlib.sha256(compiler.read_bytes()).hexdigest()==digest
    report={'compiler_sha256':digest,'compiler_unchanged_during_run':stable,'execution_timeout_seconds':args.timeout,'jobs':args.jobs,'summary':summary,'results':sorted(results,key=lambda x:x['case'])}
    if args.report:args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n')
    print('SUMMARY',summary,flush=True)
    return bool(summary['failed'] or summary['timeout'] or not stable)
if __name__=='__main__':raise SystemExit(main())
