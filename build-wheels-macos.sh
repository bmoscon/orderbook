#!/bin/bash
set -e

PY_VERS="3.12 3.13 3.14"
DEST="./wheelhouse"

for V in ${PY_VERS}; do
    uv build --wheel --python "${V}" --out-dir "${DEST}"
done
