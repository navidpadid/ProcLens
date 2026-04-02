#!/usr/bin/env bash
set -euo pipefail

major="${RELEASE_MAJOR:-false}"
minor="${RELEASE_MINOR:-false}"
patch="${RELEASE_PATCH:-false}"

count=0
[[ "$major" == "true" ]] && count=$((count + 1))
[[ "$minor" == "true" ]] && count=$((count + 1))
[[ "$patch" == "true" ]] && count=$((count + 1))

if [[ $count -gt 1 ]]; then
	echo "Error: Multiple release labels detected. Only one release label (major, minor, or patch) is allowed."
	exit 1
fi

echo "Label validation passed: $count release label(s) found"
