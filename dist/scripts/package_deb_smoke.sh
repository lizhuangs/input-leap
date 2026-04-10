#!/usr/bin/env bash

set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <build-dir> <cpack-command>" >&2
    exit 2
fi

build_dir=$1
cpack_command=$2

find "$build_dir" -maxdepth 1 -type f -name '*.deb' -delete

(
    cd "$build_dir"
    "$cpack_command" -G DEB --config CPackConfig.cmake
)

if ! find "$build_dir" -maxdepth 1 -type f -name '*.deb' | grep -q .; then
    echo "cpack -G DEB did not produce any .deb packages" >&2
    exit 1
fi
