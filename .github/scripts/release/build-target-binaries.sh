#!/usr/bin/env bash
set -euo pipefail

target_arch="${TARGET_ARCH:-}"
release_tag="${RELEASE_TAG:-}"
target_kernel_base="${TARGET_KERNEL_BASE:-}"

if [[ -z "$target_arch" || -z "$release_tag" || -z "$target_kernel_base" ]]; then
	echo "TARGET_ARCH, RELEASE_TAG, and TARGET_KERNEL_BASE are required"
	exit 1
fi

kdir="$(ls -d /usr/src/linux-headers-${target_kernel_base}-*-generic 2>/dev/null | sort -V | tail -n 1)"

if [[ -z "${kdir:-}" ]]; then
	echo "No Ubuntu ${target_kernel_base}.x generic kernel headers found"
	exit 1
fi

echo "Using kernel headers: $kdir"

make KDIR="$kdir" PROCLENS_VERSION="$release_tag" clean
make KDIR="$kdir" PROCLENS_VERSION="$release_tag" module
make PROCLENS_VERSION="$release_tag" user

mkdir -p "out/${target_arch}"
cp build/proclens_module.ko "out/${target_arch}/proclens_module.ko"
cp build/proclens "out/${target_arch}/proclens"
