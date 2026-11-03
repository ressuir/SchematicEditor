#!/bin/sh
set -eu
APP_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -x "$APP_DIR/bin/linux/SchematicEditor" ]; then
  exec "$APP_DIR/bin/linux/SchematicEditor" "$@"
fi
if [ -x "$APP_DIR/build/SchematicEditor" ]; then
  exec "$APP_DIR/build/SchematicEditor" "$@"
fi
printf '%s\n' 'SchematicEditor executable not found. Build using the commands in README.md.' >&2
exit 1
