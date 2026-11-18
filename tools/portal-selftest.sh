#!/bin/sh
# Headless portal harness: private D-Bus session plus the Qt Test suite.
# Builds its own targets so local runs and CI use the same entry point.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${HERMES_PORTAL_BUILD_DIR:-$root/build/cmake}
backend=$build/hermes-portal-backend
selftest=$build/hermes-portal-selftest

# A working private session bus is the precondition for every portal test.
dbus-run-session -- true

cmake --build "$build" --target hermes-portal-backend hermes-portal-selftest

dbus-run-session -- "$backend" --help
dbus-run-session -- "$selftest"
