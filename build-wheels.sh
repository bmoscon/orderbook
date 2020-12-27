#!/bin/bash
set -e -x

py_vers=("/opt/python/cp37-cp37m/bin" "/opt/python/cp38-cp38/bin" "/opt/python/cp39-cp39/bin")

for PY in "${py_vers[@]}"; do
    "${PY}/pip" wheel /io/ -w wheelhouse/
done

for whl in wheelhouse/*.whl; do
    auditwheel repair "$whl" -w /io/wheelhouse/
done
