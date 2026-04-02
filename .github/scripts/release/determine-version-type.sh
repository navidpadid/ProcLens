#!/usr/bin/env bash
set -euo pipefail

event_name="${EVENT_NAME:-}"
input_version_type="${INPUT_VERSION_TYPE:-}"
major="${RELEASE_MAJOR:-false}"
minor="${RELEASE_MINOR:-false}"
patch="${RELEASE_PATCH:-false}"

if [[ -z "${GITHUB_OUTPUT:-}" ]]; then
	echo "GITHUB_OUTPUT is required"
	exit 1
fi

if [[ "$event_name" == "workflow_dispatch" ]]; then
	echo "type=$input_version_type" >> "$GITHUB_OUTPUT"
elif [[ "$major" == "true" ]]; then
	echo "type=major" >> "$GITHUB_OUTPUT"
elif [[ "$minor" == "true" ]]; then
	echo "type=minor" >> "$GITHUB_OUTPUT"
elif [[ "$patch" == "true" ]]; then
	echo "type=patch" >> "$GITHUB_OUTPUT"
else
	echo "type=none" >> "$GITHUB_OUTPUT"
fi
