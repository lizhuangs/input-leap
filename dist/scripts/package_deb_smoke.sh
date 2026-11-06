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

deb_file=$(find "$build_dir" -maxdepth 1 -type f -name '*.deb' | head -n 1)

package_listing="$build_dir/deb-listing.txt"
dpkg-deb -c "$deb_file" >"$package_listing"

grep -q './usr/bin/input-leap-systemd-service$' "$package_listing" || {
    echo "missing packaged launcher binary in $deb_file" >&2
    exit 1
}

grep -q './usr/lib/systemd/system/input-leap.service$' "$package_listing" || {
    echo "missing systemd unit in $deb_file" >&2
    exit 1
}

control_dir="$build_dir/deb-control"
rm -rf "$control_dir"
mkdir -p "$control_dir"
dpkg-deb -e "$deb_file" "$control_dir"

grep -q 'systemctl enable input-leap.service' "$control_dir/postinst" || {
    echo "postinst does not enable input-leap.service" >&2
    exit 1
}

grep -q 'systemctl disable --now input-leap.service' "$control_dir/prerm" || {
    echo "prerm does not disable input-leap.service" >&2
    exit 1
}
