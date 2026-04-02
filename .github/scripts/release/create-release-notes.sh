#!/usr/bin/env bash
set -euo pipefail

version="${VERSION:-}"
bump_type="${BUMP_TYPE:-}"
event_name="${EVENT_NAME:-}"
target_kernel_base="${TARGET_KERNEL_BASE:-}"

if [[ -z "$version" || -z "$bump_type" || -z "$event_name" || -z "$target_kernel_base" ]]; then
	echo "VERSION, BUMP_TYPE, EVENT_NAME, and TARGET_KERNEL_BASE are required"
	exit 1
fi

if [[ "$event_name" == "pull_request" ]]; then
	pr_title="$(jq -r '.pull_request.title // ""' "$GITHUB_EVENT_PATH")"
	pr_body="$(jq -r '.pull_request.body // ""' "$GITHUB_EVENT_PATH")"

	{
		printf '## Changelog\n\n'
		printf '### %s\n\n' "$pr_title"
		printf '%s\n\n' "$pr_body"
		printf '\n---\n'
		printf '**Version**: %s | **Type**: %s\n\n' "$version" "$bump_type"
		printf '## Binary package includes\n\n'
		printf -- '- `amd64/proclens_module.ko`\n'
		printf -- '- `amd64/proclens`\n'
		printf -- '- `arm64/proclens_module.ko`\n'
		printf -- '- `arm64/proclens`\n'
		printf -- '- `install.sh`\n'
		printf -- '- `uninstall.sh`\n'
		printf -- '- `README-QUICKSTART.md`\n\n'
		printf 'Quick Start summary:\n\n'
		printf -- '- Supports Linux kernel `%s.x`\n' "$target_kernel_base"
		printf -- '- Includes binaries for `amd64` (`x86_64`) and `arm64` (`aarch64`)\n'
		printf -- '- Install or update: `sudo ./install.sh`\n'
		printf -- '- Run user tool: `sudo proclens`\n'
		printf -- '- Uninstall: `sudo ./uninstall.sh`\n'
	} > /tmp/release_notes.md
else
	{
		printf '## Changelog\n\n'
		printf -- '- Manual release triggered via workflow dispatch.\n\n'
		printf '**Version**: %s | **Type**: %s\n\n' "$version" "$bump_type"
		printf '## Binary package includes\n\n'
		printf -- '- `amd64/proclens_module.ko`\n'
		printf -- '- `amd64/proclens`\n'
		printf -- '- `arm64/proclens_module.ko`\n'
		printf -- '- `arm64/proclens`\n'
		printf -- '- `install.sh`\n'
		printf -- '- `uninstall.sh`\n'
		printf -- '- `README-QUICKSTART.md`\n\n'
		printf 'Quick Start summary:\n\n'
		printf -- '- Supports Linux kernel `%s.x`\n' "$target_kernel_base"
		printf -- '- Includes binaries for `amd64` (`x86_64`) and `arm64` (`aarch64`)\n'
		printf -- '- Install or update: `sudo ./install.sh`\n'
		printf -- '- Run user tool: `sudo proclens`\n'
		printf -- '- Uninstall: `sudo ./uninstall.sh`\n'
	} > /tmp/release_notes.md
fi

cat /tmp/release_notes.md
