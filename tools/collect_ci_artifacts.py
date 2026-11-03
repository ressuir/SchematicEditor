#!/usr/bin/env python3
import hashlib,json,os,subprocess,tarfile
from pathlib import Path
import xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[1];out=root/'ci-artifacts';out.mkdir(exist_ok=True)
dist=root/'dist';dist.mkdir(exist_ok=True)
revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
def suites(path,expected):
    if not path.is_file():return {'status':'unverified','reason':'test result file absent'}
    cases=ET.parse(path).findall('.//testcase');results={}
    for case in cases:
        state='skipped' if case.find('skipped') is not None or case.get('status')=='notrun' else 'failed' if case.find('failure') is not None or case.find('error') is not None else 'passed'
        results[case.get('name')]=state
    return {'status':'passed' if set(results)==set(expected) and all(s=='passed' for s in results.values()) else 'not_passed','cases':results}
def external(path):
    if not path.is_file():return {'status':'unverified','reason':'test result file absent'}
    result=json.loads(path.read_text());return {'status':'passed' if result.get('verified') is True else 'not_passed','result':result}
status={'source_revision':revision,'github_run_url':None,'core':suites(out/'core-junit.xml',['model_tests','dynamic_tests','editing_tests','simulation_scenarios_tests','regression_tests','cli_integration']),
        'gui':suites(out/'gui/junit.xml',['gui_widget_tests','gui_native_tests']),
        'executable_startup':external(out/'gui/executable/result.json'),'kicad_real_import':external(out/'kicad/result.json'),
        'note':'This records only this job. Sanitizer results are in the separate sanitizer artifact. Absent or skipped tests never count as passes.'}
if os.environ.get('GITHUB_RUN_ID'):
    status['github_run_url']=f"{os.environ['GITHUB_SERVER_URL']}/{os.environ['GITHUB_REPOSITORY']}/actions/runs/{os.environ['GITHUB_RUN_ID']}"
(out/'status.json').write_text(json.dumps(status,indent=2)+'\n')
binaries={};names=['SchematicEditor','schematic-cli','model_tests','dynamic_tests','editing_tests','simulation_scenarios_tests','regression_tests','gui_widget_tests','gui_native_tests']
with tarfile.open(dist/'linux-release.tar.gz','w:gz') as archive:
    for name in names:
        path=root/'build-release'/name
        if path.is_file():archive.add(path,arcname='bin/'+name);binaries[name]=hashlib.sha256(path.read_bytes()).hexdigest()
subprocess.run(['git','archive','--format=tar.gz','--output='+str(dist/'source.tar.gz'),revision],cwd=root,check=True)
(out/'build-info.json').write_text(json.dumps({'source_revision':revision,'binaries_sha256':binaries},indent=2)+'\n')
print(json.dumps(status,indent=2))
