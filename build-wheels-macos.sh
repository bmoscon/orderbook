#!/bin/bash
set -e

PY_VERS="3.13 3.14 3.14t 3.15 3.15t"
DEST="./wheelhouse"

for V in ${PY_VERS}; do
    uv build --wheel --python "${V}" --out-dir "${DEST}"
done
