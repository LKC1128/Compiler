#!/usr/bin/env python3
"""Create a source-only submission archive with a verifiable manifest."""
from pathlib import Path
import argparse
import hashlib
import io
import json
import tarfile

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,default=ROOT/'dist/sysy-arm64-source.tar.gz')
    args=parser.parse_args()
    files=[]
    for directory in ('include','src','runtime','scripts','docs','tests'):
        for path in (ROOT/directory).rglob('*'):
            if path.is_symlink():raise RuntimeError('Submission must not contain symlinks: '+str(path))
            if path.is_file() and '__pycache__' not in path.parts and path.suffix not in ('.o','.a','.so','.pyc','.log'):
                files.append(path)
    files.extend(ROOT/name for name in ('CMakeLists.txt','Makefile','README.md','PROGRESS.md','.gitignore','.gitattributes'))
    files=sorted(set(files))
    entries=[]
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with tarfile.open(args.output,'w:gz') as archive:
        for path in files:
            data=path.read_bytes();name=path.relative_to(ROOT).as_posix()
            info=tarfile.TarInfo('sysy-compiler/'+name)
            info.size=len(data);info.mode=0o755 if path.suffix=='.sh' else 0o644
            archive.addfile(info,io.BytesIO(data))
            entries.append({'path':name,'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)})
        manifest=json.dumps({'format':1,'files':entries},ensure_ascii=False,indent=2).encode('utf-8')
        info=tarfile.TarInfo('sysy-compiler/SUBMISSION_MANIFEST.json');info.size=len(manifest);info.mode=0o644
        archive.addfile(info,io.BytesIO(manifest))
    # Reopen the archive and validate every file before reporting success.
    with tarfile.open(args.output) as archive:
        for entry in entries:
            stored=archive.extractfile('sysy-compiler/'+entry['path']).read()
            if hashlib.sha256(stored).hexdigest()!=entry['sha256']:raise RuntimeError('Archive verification failed')
    digest=hashlib.sha256(args.output.read_bytes()).hexdigest()
    args.output.with_suffix(args.output.suffix+'.sha256').write_text(digest+'  '+args.output.name+'\n')
    print(f'{args.output}: {len(entries)} files, SHA-256 {digest}')
    return 0

if __name__=='__main__':raise SystemExit(main())
