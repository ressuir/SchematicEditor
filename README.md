# SchematicEditor

C++17 / wxWidgets 3.2 schematic editor with predefined digital components, custom
component libraries, orthogonal wiring, undo/redo, XML design persistence,
event-driven digital simulation and a KiCad native netlist exporter.

This repository contains software, examples and tests. It contains no student
reports, presentation slides or student identity records.

## Build and run

On Ubuntu 24.04 or a compatible Linux system:

```bash
sudo apt-get install build-essential cmake libwxgtk3.2-dev python3
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j4
./build-release/SchematicEditor examples/dynamic-and.ses
ctest --test-dir build-release -LE gui --output-on-failure
```

The GUI needs an accessible X11/Wayland desktop. Linux binaries dynamically link
wxWidgets and the system libraries. Windows project files are provided, but no
Windows build or execution result is claimed; configure `WXWIN` in that build
environment. The CMake build embeds the exact Git source revision.

## Editing and simulation

Select a library component and click the canvas to place it. Esc ends placement.
W selects wiring; click endpoints/bends, then Esc or Enter to finish. Drag a
selected component to move it. Delete removes selections; Ctrl+Z/Y undo/redo;
Ctrl+C/X/V use the application's internal clipboard. The wheel zooms and the
middle button pans. Ctrl+S saves, Ctrl+O opens, Ctrl+Shift+S saves as.

The property panel edits references, initial input values, footprints, physical
pad numbers, propagation delays and custom truth tables. Import custom library
XML from Preferences; `examples/sample-library.xml` includes executable models.
Placed custom components persist their pin definitions and logic models inside
the `.ses` file; a reopened design does not depend on the library being loaded.

F5 runs/resumes, F6 pauses and advances one tick, Shift+F5 stops. Space or a double
click toggles a selected live input. Gates schedule delayed output events; input
changes propagate through later ticks. Clock edges, unknown values, conflicting
drivers and inertial pulse cancellation are supported. Edits and undo/redo stop
simulation so it cannot retain an obsolete circuit snapshot. This is a digital
logic simulator; analog symbols do not have an analog solver.

```bash
./build-release/schematic-cli --version
./build-release/schematic-cli simulate examples/dynamic-and.ses 10 2:1:1 4:2:1 7:1:0
./build-release/schematic-cli export-kicad examples/dynamic-and.ses output.net
```

PCB export requires `Library:Footprint` and unique physical pad numbers. Default
footprints represent module terminals using standard single-row 2.54 mm headers;
they do not claim a physical 74xx IC pinout. Diagnostic netlists shown in the
Inspect menu are separate from the KiCad export.

## Automated verification

`.github/workflows/ci.yml` uses only the standard `ubuntu-24.04` runner and refuses
private-repository jobs. GitHub documents standard public-repository runners as
[free](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).

The workflow builds Release, runs six non-GUI suites, runs the applicable suites
with ASan/UBSan, and creates an Xvfb desktop for GUI tests. `gui_widget_tests`
checks wx widget callbacks; `gui_native_tests` sends native mouse/key input through
the placement → wire → drag → undo/redo → save dialog → new → open dialog workflow,
checks circuit state and connectivity, and exercises the actual simulation timer.
A separate smoke test starts and closes the shipped application executable.

The KiCad test installs the real PCB Editor and footprint library, uses its
File → Import → Netlist dialog, saves the resulting PCB and checks every imported
reference, footprint identifier and pad network through KiCad's own bindings.
Missing dependencies, skipped GUI tests or absent result files do not count as
passes. The ASan/UBSan job disables leak detection and makes no leak-test claim.

Workflow configuration and compiled test code are not evidence that a remote run
succeeded. Consult the actual Actions run for its exact commit, conclusions,
JUnit files, logs, native screenshots, imported PCB, source archive and binaries.
No successful Actions run is claimed merely by this README.

```bash
sudo apt-get install xvfb xauth xdotool openbox dbus-x11 scrot fonts-noto-cjk
xvfb-run -a -s '-screen 0 1600x1000x24 -nolisten tcp' dbus-run-session -- bash tools/run_gui_ci.sh
```

The full reachable Git history is scanned before publication and in CI with
`tools/check_public_history.py`; third-party copyright notices remain intact.
The original supplied application had no explicit project license. No license
has been invented for it. Dependency licenses and source-format references are
listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and `licenses/`.
