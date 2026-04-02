#!/usr/bin/env bash
set -euo pipefail

release_tag="${RELEASE_TAG:-}"
release_version="${RELEASE_VERSION:-}"
target_kernel_base="${TARGET_KERNEL_BASE:-}"

if [[ -z "${GITHUB_OUTPUT:-}" ]]; then
	echo "GITHUB_OUTPUT is required"
	exit 1
fi

if [[ -z "$release_tag" || -z "$release_version" || -z "$target_kernel_base" ]]; then
	echo "RELEASE_TAG, RELEASE_VERSION, and TARGET_KERNEL_BASE are required"
	exit 1
fi

pkg_name="proclens_module-binaries-${release_tag}-linux-${target_kernel_base}"
pkg_dir="dist/${pkg_name}"
archive_path="dist/${pkg_name}.tar.gz"

mkdir -p "$pkg_dir/amd64" "$pkg_dir/arm64"

cp dist/artifacts/binaries-amd64/proclens_module.ko "$pkg_dir/amd64/"
cp dist/artifacts/binaries-amd64/proclens "$pkg_dir/amd64/"
cp dist/artifacts/binaries-arm64/proclens_module.ko "$pkg_dir/arm64/"
cp dist/artifacts/binaries-arm64/proclens "$pkg_dir/arm64/"

chmod +x "$pkg_dir/amd64/proclens" "$pkg_dir/arm64/proclens"

cat > "$pkg_dir/install.sh" << 'SCRIPT'
#!/usr/bin/env bash
set -euo pipefail

usage() {
	cat << 'USAGE'
Usage: sudo ./install.sh [--arch amd64|arm64]

Installs or updates both components:
  - Kernel module: /lib/modules/$(uname -r)/extra/proclens_module.ko
  - User binary:   /usr/local/bin/proclens

Behavior:
  - Detects architecture automatically (x86_64 -> amd64, aarch64 -> arm64)
  - If module is already loaded, unloads it first
  - Replaces previously installed binaries (update-in-place)
  - Runs depmod and loads module with modprobe (fallback: insmod)
USAGE
}

if [[ "${EUID}" -ne 0 ]]; then
	echo "Please run as root: sudo ./install.sh"
	exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

arch=""
if [[ $# -gt 0 ]]; then
	case "$1" in
		--arch)
			arch="${2:-}"
			shift 2
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			echo "Unknown option: $1"
			usage
			exit 1
			;;
	esac
fi

if [[ -z "$arch" ]]; then
	case "$(uname -m)" in
		x86_64)
			arch="amd64"
			;;
		aarch64|arm64)
			arch="arm64"
			;;
		*)
			echo "Unsupported architecture: $(uname -m)"
			echo "Use --arch amd64|arm64"
			exit 1
			;;
	esac
fi

if [[ "$arch" != "amd64" && "$arch" != "arm64" ]]; then
	echo "Invalid --arch value: $arch"
	echo "Use amd64 or arm64"
	exit 1
fi

src_mod="$script_dir/$arch/proclens_module.ko"
src_bin="$script_dir/$arch/proclens"

if [[ ! -f "$src_mod" || ! -f "$src_bin" ]]; then
	echo "Missing release binaries for arch '$arch'"
	exit 1
fi

krel="$(uname -r)"
mod_dir="/lib/modules/$krel/extra"
mod_dst="$mod_dir/proclens_module.ko"
bin_dst="/usr/local/bin/proclens"

echo "Installing ProcLens binaries for arch: $arch"
echo "Kernel release: $krel"

if lsmod | grep -q '^proclens_module '; then
	echo "Module is currently loaded; unloading old version..."
	modprobe -r proclens_module || rmmod proclens_module
fi

mkdir -p "$mod_dir"
install -m 0644 "$src_mod" "$mod_dst"
install -m 0755 "$src_bin" "$bin_dst"

depmod "$krel"

if ! insmod "$mod_dst"; then
	echo "insmod failed; trying modprobe..."
	modprobe proclens_module
fi

echo "Install/update complete."
echo "User binary: $bin_dst"
echo "Module file: $mod_dst"
echo "Check module version: cat /sys/module/proclens_module/version"
echo "Check user version:   sudo proclens --version"
SCRIPT

cat > "$pkg_dir/uninstall.sh" << 'SCRIPT'
#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
	echo "Please run as root: sudo ./uninstall.sh"
	exit 1
fi

krel="$(uname -r)"
mod_dst="/lib/modules/$krel/extra/proclens_module.ko"
bin_dst="/usr/local/bin/proclens"

if lsmod | grep -q '^proclens_module '; then
	echo "Unloading proclens_module module..."
	modprobe -r proclens_module || rmmod proclens_module || true
fi

rm -f "$mod_dst"
rm -f "$bin_dst"
depmod "$krel"

echo "Uninstall complete."
SCRIPT

chmod +x "$pkg_dir/install.sh" "$pkg_dir/uninstall.sh"

cat > "$pkg_dir/README-QUICKSTART.md" << 'DOC'
# Quick Start (Linux kernel __TARGET_KERNEL_BASE__.x)

This package contains prebuilt binaries for:
- amd64 (x86_64)
- arm64 (aarch64)

Version: __RELEASE_VERSION__

## 1) Choose architecture

On target machine:

```bash
uname -m
```

Use:
- `amd64/` for `x86_64`
- `arm64/` for `aarch64`

## 2) Install or update

```bash
sudo ./install.sh
```

The installer:
- Installs/updates `proclens` to `/usr/local/bin/proclens`
- Installs/updates `proclens_module.ko` to `/lib/modules/$(uname -r)/extra/proclens_module.ko`
- Unloads an already-loaded `proclens_module` before update
- Runs `depmod` and loads the module

Optional (override architecture selection):

```bash
sudo ./install.sh --arch amd64
# or
sudo ./install.sh --arch arm64
```

If Secure Boot is enabled, module signing may be required.

## 3) Run user program

```bash
sudo proclens --version
sudo proclens
```

## 4) Uninstall

```bash
sudo ./uninstall.sh
```
DOC

sed -i \
	-e "s/__TARGET_KERNEL_BASE__/${target_kernel_base}/g" \
	-e "s/__RELEASE_VERSION__/${release_version}/g" \
	"$pkg_dir/README-QUICKSTART.md"

tar -czf "$archive_path" -C dist "$pkg_name"
sha256sum "$archive_path" > "${archive_path}.sha256"

echo "archive_path=$archive_path" >> "$GITHUB_OUTPUT"
echo "checksum_path=${archive_path}.sha256" >> "$GITHUB_OUTPUT"
