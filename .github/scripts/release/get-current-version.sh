#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${GITHUB_OUTPUT:-}" ]]; then
	echo "GITHUB_OUTPUT is required"
	exit 1
fi

latest_tag="$(git describe --tags --abbrev=0 2>/dev/null || echo "v0.0.0")"
version="${latest_tag#v}"

echo "tag=$latest_tag" >> "$GITHUB_OUTPUT"
echo "version=$version" >> "$GITHUB_OUTPUT"
echo "Current version: $version"
