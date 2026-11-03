#!/usr/bin/env python3
"""Exercise the shipped executable separately from the GUI test harness."""
import argparse,json,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('editor',type=Path);p.add_argument('design',type=Path);p.add_argument('output',type=Path);args=p.parse_args()
args.output.mkdir(parents=True,exist_ok=True)
result={'verified':False,'scope':'executable startup only; native interaction is a separate GUI suite'}
with (args.output/'process.log').open('w') as log:
    process=subprocess.Popen([str(args.editor.resolve()),str(args.design.resolve())],stdout=log,stderr=subprocess.STDOUT)
    try:
        window=None;deadline=time.monotonic()+20
        while time.monotonic()<deadline and process.poll() is None:
            query=subprocess.run(['xdotool','search','--onlyvisible','--pid',str(process.pid),'--name','Schematic'],capture_output=True,text=True)
            if query.returncode==0 and query.stdout.strip():window=query.stdout.splitlines()[0];break
            time.sleep(.2)
        assert window is not None,'executable did not display its real editor window'
        subprocess.run(['xdotool','windowactivate','--sync',window],check=True,timeout=10)
        time.sleep(.5);subprocess.run(['scrot','-o',str(args.output/'editor-startup.png')],check=True,timeout=10)
        subprocess.run(['xdotool','key','--clearmodifiers','alt+F4'],check=True,timeout=10)
        assert process.wait(timeout=15)==0,'application did not exit cleanly'
        result['verified']=True
    except Exception as exc:result['error']=str(exc)
    finally:
        if process.poll() is None:process.terminate();process.wait(timeout=5)
(args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result));raise SystemExit(0 if result['verified'] else 1)
