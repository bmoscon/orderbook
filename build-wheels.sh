#!/bin/bash
set -e -x

py_vers=("/opt/python/cp310-cp310/bin" "/opt/python/cp311-cp311/bin" "/opt/python/cp312-cp312/bin")

for PY in "${py_vers[@]}"; do
    "${PY}/pip" wheel /io/ -w wheelhouse/
done

for whl in wheelhouse/*.whl; do
    auditwheel repair "$whl" -w /io/wheelhouse/
done
