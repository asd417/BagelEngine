#!/usr/bin/env bash
# Creates a Python venv next to this script and installs CoACD into it, so the
# collision baker (partsParser --coacd) can shell out to coacd_decompose.py.
# One-time setup; re-run to upgrade. Requires python3 on PATH.
set -e
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV="$HERE/.venv"

if ! command -v python3 >/dev/null 2>&1; then
    echo "ERROR: python3 not found on PATH."
    exit 1
fi

if [ ! -x "$VENV/bin/python" ]; then
    echo "=== Creating venv at $VENV ==="
    python3 -m venv "$VENV"
fi

echo "=== Installing coacd + numpy ==="
"$VENV/bin/python" -m pip install --upgrade pip
"$VENV/bin/python" -m pip install coacd numpy

echo "[OK] CoACD venv ready: $VENV"
