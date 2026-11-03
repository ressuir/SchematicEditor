#!/usr/bin/env python3
"""Run the delivered CLI, inspect real traces, and independently parse its netlist.
This deliberately does not claim to be a KiCad/PCB import test.
"""
import csv,io,json,re,subprocess,sys,tempfile,xml.etree.ElementTree as ET
from pathlib import Path
cli=Path(sys.argv[1]);examples=Path(sys.argv[2]);checks=0
def run(*args,ok=True):
 p=subprocess.run([str(cli),*map(str,args)],text=True,capture_output=True)
 assert (p.returncode==0)==ok,(args,p.returncode,p.stdout,p.stderr)
 return p.stdout
def check(value,label):
 global checks
 assert value,label
 checks+=1;print('PASS',label)
def tree(text):
 tokens=re.findall(r'"(?:\\.|[^"\\])*"|[()]|[^\s()]+',text);i=0
 def parse():
  nonlocal i
  assert i<len(tokens);t=tokens[i];i+=1
  if t=='(':
   a=[]
   while i<len(tokens) and tokens[i]!=')':a.append(parse())
   assert i<len(tokens);i+=1;return a
  assert t!=')'
  return json.loads(t) if t.startswith('"') else t
 result=parse();assert i==len(tokens);return result
def children(node,name):return [x for x in node[1:] if isinstance(x,list) and x and x[0]==name]
def child(node,name):
 a=children(node,name);assert len(a)==1,(name,a);return a[0]
p=examples/'dynamic-and.ses'
rows=list(csv.DictReader(io.StringIO(run('simulate',p,10,'2:1:1','4:2:1','7:1:0'))))
check(rows[0]['N3']=='X' and rows[1]['N3']=='0','engine starts unknown and settles after gate delay')
check(rows[4]['N1']=='1' and rows[4]['N2']=='1' and rows[4]['N3']=='0' and rows[5]['N3']=='1','real input events precede AND output transition')
check(rows[7]['N3']=='1' and rows[8]['N3']=='0','live reset propagates on next step')
clock=list(csv.DictReader(io.StringIO(run('simulate',examples/'clock-and.ses',10))))
check([r['N1'] for r in clock[:5]]==['0','0','1','1','0'],'clock advances independently of static truth evaluation')
check(clock[2]['N3']=='0' and clock[3]['N3']=='1' and clock[5]['N3']=='0','clock edges propagate through gate with delay')
run('simulate',p,5,'1:1:1:',ok=False);run('simulate',p,-1,ok=False);run('simulate',p,5,'1:3:1',ok=False);run('simulate',p,5,'9:1:0',ok=False)
check(True,'CLI rejects negative duration, non-input events and out-of-range event times')
with tempfile.TemporaryDirectory(prefix='se-cli-') as temp:
 temp=Path(temp);saved=temp/'saved.ses';net=temp/'export.net'
 run('roundtrip',p,saved)
 a=ET.parse(p).getroot();b=ET.parse(saved).getroot()
 def sem(node):return (node.tag,dict(node.attrib),[sem(x) for x in node])
 check(sem(a)==sem(b),'save/reopen preserves complete design metadata')
 run('export-kicad',saved,net);n=tree(net.read_text())
 check(n[0]=='export' and child(n,'version')[1]=='E','independent parser accepts native KiCad netlist structure')
 comps=children(child(n,'components'),'comp');byref={child(c,'ref')[1]:c for c in comps}
 check(set(byref)=={'IN1','IN2','U3'},'exported physical references match saved design')
 check(all(child(c,'footprint')[1].startswith('Connector_PinHeader_2.54mm:') for c in comps),'every board component carries an explicit footprint identifier')
 groups=[];pads=[]
 for node in children(child(n,'nets'),'net'):
  group={(child(t,'ref')[1],child(t,'pin')[1]) for t in children(node,'node')};groups.append(group);pads.extend(group)
 check({frozenset(g) for g in groups}=={frozenset({('IN1','1'),('U3','1')}),frozenset({('IN2','1'),('U3','2')}),frozenset({('U3','3')})},'every exported pad and connection matches the actual circuit')
 check(len(pads)==len(set(pads))==5,'no physical pad is omitted or assigned to multiple networks')
 for comp in comps:
  check(bool(re.fullmatch('[0-9a-f]{8}-(?:[0-9a-f]{4}-){3}[0-9a-f]{12}',child(comp,'tstamps')[1])),'component uses stable valid UUID')
print(f'PASS {checks} CLI/format checks; actual target PCB import is a separate unverified acceptance gate')
