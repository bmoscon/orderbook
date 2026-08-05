#!/bin/bash
set -e

docker run --rm --pull always --platform linux/amd64 -v "$(pwd)":/io quay.io/pypa/manylinux_2_28_x86_64 /io/build-wheels.sh
