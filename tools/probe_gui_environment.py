#!/usr/bin/env python3
"""Record display/socket diagnostics without changing desktop configuration.

Run: python3 tools/probe_gui_environment.py build-release OUTPUT_DIRECTORY
Exit 0 means diagnostics were written; consult results, not this exit code, for acceptance.
"""
import datetime
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile

repo = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
result = {"utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
          "environment": {k: os.environ.get(k) for k in
                          ("DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "DBUS_SESSION_BUS_ADDRESS")},
          "socket_probes": {}, "commands": {},
          "note": "Diagnostics only. Socket/display availability is not GUI or PCB acceptance."}
status = Path("/proc/self/status")
if status.exists():
    result["process_restrictions"] = [s for s in status.read_text().splitlines()
                                      if s.startswith(("Seccomp", "NoNewPrivs"))]

def probe(name, family, operation, address):
    try:
        with socket.socket(family, socket.SOCK_STREAM) as sock:
            sock.settimeout(2)
            getattr(sock, operation)(address)
        result["socket_probes"][name] = {"succeeded": True}
    except OSError as exc:
        result["socket_probes"][name] = {"succeeded": False, "errno": exc.errno, "error": str(exc)}

with tempfile.TemporaryDirectory(prefix="schematic-socket-probe-") as tmp:
    probe("unix_bind", socket.AF_UNIX, "bind", str(Path(tmp)/"display.sock"))
probe("ipv4_bind", socket.AF_INET, "bind", ("127.0.0.1", 0))
for path in sorted(Path("/tmp/.X11-unix").glob("X*")):
    probe("connect_"+str(path), socket.AF_UNIX, "connect", str(path))

def run(name, command, timeout=15):
    entry = {"argv": command}
    try:
        p = subprocess.run(command, cwd=repo, text=True, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=timeout)
        entry["exit_code"] = p.returncode
        output = p.stdout
    except subprocess.TimeoutExpired as exc:
        entry["timed_out"] = True
        output = exc.stdout or b""
        if isinstance(output, bytes): output = output.decode(errors="replace")
    except OSError as exc:
        entry["error"] = str(exc)
        output = str(exc)+"\n"
    (out/(name+".log")).write_text(output)
    result["commands"][name] = entry

run("gui-widget-direct", [str(build/"gui_widget_tests"), str(repo/"examples"), str(out)])
if shutil.which("xdpyinfo"): run("desktop-display", ["xdpyinfo"])
if shutil.which("xvfb-run"):
    run("gui-widget-xvfb", ["xvfb-run", "-a", "-e", str(out/"xvfb-server.log"),
                           str(build/"gui_widget_tests"), str(repo/"examples"), str(out)])
result["pcb_binaries"] = {name: shutil.which(name) for name in ("kicad-cli", "pcbnew", "pcb-rnd")}
(out/"environment-probe.json").write_text(json.dumps(result, ensure_ascii=False, indent=2)+"\n")
print(json.dumps(result, ensure_ascii=False, indent=2))
