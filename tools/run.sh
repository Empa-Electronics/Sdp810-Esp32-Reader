#!/bin/sh
# Runs the plotter with its own venv, independent of any active ESP-IDF environment.
DIR="$(cd "$(dirname "$0")" && pwd)"
exec "$DIR/.venv/bin/python" "$DIR/sdp_plotter.py" "$@"
