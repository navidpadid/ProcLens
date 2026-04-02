#!/usr/bin/env bash
set -euo pipefail

tag="${TAG:-}"

if [[ -z "$tag" ]]; then
	echo "TAG is required"
	exit 1
fi

if git rev-parse "$tag" >/dev/null 2>&1; then
	echo "Tag $tag already exists, skipping tag creation"
	exit 0
fi

git tag -a "$tag" -m "Release $tag"
git push origin "$tag"
