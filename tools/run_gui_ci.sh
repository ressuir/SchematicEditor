#!/usr/bin/env bash
set -euo pipefail
export GDK_BACKEND=x11 GTK_USE_PORTAL=0
unset WAYLAND_DISPLAY
mkdir -p ci-artifacts/gui
openbox > ci-artifacts/gui/window-manager.log 2>&1 &
wm_pid=$!
cleanup() {
  cp -a build-release/gui-artifacts ci-artifacts/gui/widget 2>/dev/null || true
  cp -a build-release/gui-native-artifacts ci-artifacts/gui/native 2>/dev/null || true
  kill "$wm_pid" 2>/dev/null || true
}
trap cleanup EXIT
ctest --test-dir build-release -L gui -V --output-junit "$PWD/ci-artifacts/gui/junit.xml" 2>&1 | tee ci-artifacts/gui/tests.log
python3 tools/assert_junit.py ci-artifacts/gui/junit.xml gui_widget_tests gui_native_tests
python3 tests/app_startup.py build-release/SchematicEditor examples/dynamic-and.ses ci-artifacts/gui/executable
