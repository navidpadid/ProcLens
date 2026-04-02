#!/usr/bin/env bash
set -euo pipefail

current="${CURRENT_VERSION:-}"
type="${VERSION_TYPE:-}"

if [[ -z "${GITHUB_OUTPUT:-}" ]]; then
	echo "GITHUB_OUTPUT is required"
	exit 1
fi

if [[ ! "$current" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
	echo "Invalid current version: $current"
	exit 1
fi

IFS='.' read -r major minor patch <<< "$current"

case "$type" in
	major)
		major=$((major + 1))
		minor=0
		patch=0
		;;
	minor)
		minor=$((minor + 1))
		patch=0
		;;
	patch)
		patch=$((patch + 1))
		;;
	*)
		echo "No valid version type found: $type"
		exit 1
		;;
esac

new_version="$major.$minor.$patch"
echo "version=$new_version" >> "$GITHUB_OUTPUT"
echo "tag=v$new_version" >> "$GITHUB_OUTPUT"
echo "New version: $new_version"
