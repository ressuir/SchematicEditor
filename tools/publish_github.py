#!/usr/bin/env python3
"""Create a NEW public repository, push real history and collect an actual CI run.

Uses an existing authenticated gh installation; never reads or prints tokens.
No existing repository is reused, force-pushed, deleted or reconfigured.
Evidence is local and excluded by .gitignore. Network or auth failures stop safely.
"""
import argparse,datetime,json,re,shutil,subprocess,sys,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--name',default='SchematicEditor');args=p.parse_args()
repo=Path(__file__).resolve().parents[1];evidence=repo/'ci-artifacts/publication';evidence.mkdir(parents=True,exist_ok=True)
state={'repository_created':False,'source_pushed':False,'actions_run':None,'actions_conclusion':None}
def command(arguments,check=True):
    process=subprocess.run(arguments,cwd=repo,text=True,capture_output=True)
    if check and process.returncode:raise RuntimeError(process.stderr.strip() or process.stdout.strip() or repr(arguments))
    return process
def json_command(arguments):return json.loads(command(arguments).stdout)
def save(): (evidence/'result.json').write_text(json.dumps(state,indent=2)+'\n')
try:
    if not re.fullmatch(r'[A-Za-z0-9_.-]+',args.name):raise RuntimeError('Repository name must be a single repository slug')
    if not shutil.which('gh'):raise RuntimeError('GitHub CLI (gh) is not installed; repository creation was not executed')
    if command(['git','status','--porcelain']).stdout.strip():raise RuntimeError('Commit reviewed source changes before publishing')
    revision=command(['git','rev-parse','HEAD']).stdout.strip();state['source_revision']=revision
    command(['python3','tools/check_public_history.py','--output',str(evidence/'history-scan.json')])
    command(['gh','auth','status'])
    login=command(['gh','api','user','--jq','.login']).stdout.strip()
    if not login or '/' in login:raise RuntimeError('Cannot identify authenticated GitHub account')
    full_name=login+'/'+args.name
    existing=command(['gh','api','repos/'+full_name],check=False)
    if existing.returncode==0:
        full_name+='-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d-%H%M%S')
        check=command(['gh','api','repos/'+full_name],check=False)
        if check.returncode==0:raise RuntimeError('Alternative repository name also exists; no repository modified')
        if '404' not in check.stderr:raise RuntimeError('Cannot determine whether alternative repository exists: '+check.stderr.strip())
    elif '404' not in existing.stderr:
        raise RuntimeError('Repository availability check failed: '+existing.stderr.strip())
    command(['gh','repo','create',full_name,'--public','--description','C++ wxWidgets schematic editor with delayed digital simulation and automated verification'])
    state.update(repository_created=True,repository=full_name);save()
    metadata=json_command(['gh','repo','view',full_name,'--json','nameWithOwner,visibility,url'])
    if metadata['nameWithOwner'].lower()!=full_name.lower() or metadata['visibility']!='PUBLIC':
        raise RuntimeError('New repository ownership/visibility did not match the requested target')
    state.update(repository_created=True,repository=full_name,repository_url=metadata['url']);save()
    # Per-command credential helper only; no global config change and no token in argv.
    command(['git','-c','credential.helper=','-c','credential.helper=!gh auth git-credential','push',metadata['url']+'.git','HEAD:refs/heads/main'])
    pushed=command(['gh','api','repos/'+full_name+'/commits/main','--jq','.sha']).stdout.strip()
    if pushed!=revision:raise RuntimeError('Remote commit differs from the reviewed local source')
    state['source_pushed']=True;save()
    deadline=time.monotonic()+45*60;dispatch_at=time.monotonic()+90;dispatched=False
    while time.monotonic()<deadline:
        runs=json_command(['gh','run','list','--repo',full_name,'--commit',revision,'--json','databaseId,url,status,conclusion,headSha','--limit','10'])
        runs=[run for run in runs if run['headSha']==revision]
        if runs:
            run=sorted(runs,key=lambda r:r['databaseId'])[-1]
            state['actions_run']=run['url'];state['actions_status']=run['status'];save()
            print(json.dumps(run),flush=True)
            if run['status']=='completed':
                state['actions_conclusion']=run['conclusion']
                jobs=json_command(['gh','run','view',str(run['databaseId']),'--repo',full_name,'--json','url,headSha,status,conclusion,jobs'])
                (evidence/'workflow-run.json').write_text(json.dumps(jobs,indent=2)+'\n')
                logs=command(['gh','run','view',str(run['databaseId']),'--repo',full_name,'--log'],check=False)
                (evidence/'workflow.log').write_text(logs.stdout+logs.stderr)
                download=command(['gh','run','download',str(run['databaseId']),'--repo',full_name,'--dir',str(evidence/'artifacts')],check=False)
                (evidence/'artifact-download.log').write_text(download.stdout+download.stderr)
                state['artifact_download_exit_code']=download.returncode
                break
        elif not dispatched and time.monotonic()>dispatch_at:
            command(['gh','workflow','run','ci.yml','--repo',full_name,'--ref','main']);dispatched=True
        time.sleep(20)
    else:raise RuntimeError('CI run did not finish before collection timeout; no success is claimed')
except Exception as exc:
    state['error']=str(exc)
finally:save();print(json.dumps(state,indent=2),flush=True)
sys.exit(0 if state['actions_conclusion']=='success' else 1)
