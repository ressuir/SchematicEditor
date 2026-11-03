#!/usr/bin/env python3
"""Scan every reachable commit/tree/blob before publishing; never print secret values.

This heuristic check complements review, and does not claim perfect secret detection.
Third-party copyright names remain intact. Generic historical build-path literals
are reported separately from credentials or user-identifying home directories.
"""
import argparse,hashlib,json,re,subprocess
from pathlib import Path,PurePosixPath
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);args=p.parse_args()
def git(*args):return subprocess.check_output(['git',*args])
commits=git('rev-list','--all').decode().splitlines();seen=set();findings=[];warnings=[]
secrets=re.compile(r'gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|AKIA[A-Z0-9]{16}|-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----|sk-[A-Za-z0-9]{20,}')
personal=re.compile(r'/home/[A-Za-z0-9_.-]+/|姓名\s*[:：]|学号\s*[:：]|(?<!\w)1[3-9]\d{9}(?!\w)|(?<!\w)\d{17}[0-9X](?!\w)')
assignment=re.compile(r'(?i)(?:password|passwd|api[_-]?key|access[_-]?token|client[_-]?secret)\s*[:=]\s*[\"\x27]([^\"\x27\n]{8,})[\"\x27]')
for commit in commits:
    meta=git('show','-s','--format=%B%n%an%n%ae%n%cn%n%ce',commit).decode(errors='replace')
    if secrets.search(meta) or personal.search(meta):findings.append({'commit':commit,'kind':'sensitive_commit_metadata'})
    for row in git('ls-tree','-r','-z',commit).split(b'\0'):
        if not row:continue
        fields,path=row.split(b'\t',1);oid=fields.split()[2].decode();path=path.decode()
        if (path,oid) in seen:continue
        seen.add((path,oid));parts=PurePosixPath(path).parts
        forbidden=any(x in {'.aws','.ssh','.git','.vs','node_modules'} for x in parts)
        forbidden|=PurePosixPath(path).suffix.lower() in {'.user','.pem','.key','.pfx','.docx','.doc','.ppt','.pptx','.pdf'}
        forbidden|=PurePosixPath(path).name.startswith('.env') and PurePosixPath(path).name!='.env.example'
        if forbidden:findings.append({'commit':commit,'path':path,'kind':'excluded_file_type'})
        data=git('cat-file','blob',oid).decode(errors='replace')
        for number,line in enumerate(data.splitlines(),1):
            if secrets.search(line) or assignment.search(line):
                findings.append({'commit':commit,'path':path,'line':number,'kind':'potential_credential','line_sha256':hashlib.sha256(line.encode()).hexdigest()})
            if not path.startswith('licenses/') and personal.search(line):
                findings.append({'commit':commit,'path':path,'line':number,'kind':'potential_personal_information'})
            if re.search(r'[A-Z]:\\',line) and path.endswith('.vcxproj'):
                warnings.append({'commit':commit,'path':path,'line':number,'kind':'generic_historical_build_path_literal'})
result={'head':git('rev-parse','HEAD').decode().strip(),'commits_scanned':len(commits),'unique_path_blobs_scanned':len(seen),
        'blocking_findings':findings,'warnings':warnings,'passed':not findings,
        'limitations':'Heuristic full reachable-history scan plus manual review; not a guarantee that every possible secret format is detectable.'}
args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k not in {'blocking_findings','warnings'}},indent=2))
if findings:print('Publication blocked: inspect redacted findings in the scan JSON.')
raise SystemExit(1 if findings else 0)
