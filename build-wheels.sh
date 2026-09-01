#!/bin/bash
# Do not run manually (runs in the docker container). use wheels.sh to launch it
set -e -x

py_vers=("/opt/python/cp313-cp313/bin" "/opt/python/cp314-cp314/bin" "/opt/python/cp314-cp314t/bin")

rm -rf /tmp/wheelhouse
mkdir -p /tmp/wheelhouse

for PY in "${py_vers[@]}"; do
    "${PY}/pip" wheel /io/ --no-deps -w /tmp/wheelhouse/
done

for whl in /tmp/wheelhouse/*.whl; do
    auditwheel repair "$whl" -w /io/wheelhouse/
done

ls -l /io/wheelhouse/
