#!/bin/bash
# requires uv and lcov to be installed
set -e

rm -rf build/ dist/
export CFLAGS="-coverage"

uv venv
uv build --wheel --python .venv/bin/python
uv pip install --reinstall dist/*.whl pytest requests sortedcontainers

# produces the coverage data
uv run pytest tests/

cd build/temp*
lcov -c --directory . --output-file all.info
# CPython's headers define inline functions that get compiled into the extension.
# Drop them so the report reflects this project's sources only.
lcov --extract all.info '*/orderbook/*' --output-file coverage.info
genhtml coverage.info --output-directory out
open out/index.html
