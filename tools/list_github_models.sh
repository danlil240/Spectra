#!/usr/bin/env bash
set -euo pipefail

# Required:
#   export GITHUB_TOKEN=ghp_xxx
#
# Optional:
#   export GITHUB_API_VERSION=2022-11-28

: "${GITHUB_TOKEN:?GITHUB_TOKEN is required}"

API_VERSION="${GITHUB_API_VERSION:-2022-11-28}"
URL="https://models.github.ai/catalog/models"

response="$(
  curl -fsSL \
    -H "Accept: application/vnd.github+json" \
    -H "Authorization: Bearer ${GITHUB_TOKEN}" \
    -H "X-GitHub-Api-Version: ${API_VERSION}" \
    "$URL"
)"

# Pretty print a compact table
if command -v jq >/dev/null 2>&1; then
  echo "$response" | jq -r '
    .[]
    | [
        .id,
        .publisher,
        (.supported_input_modalities | join(",")),
        (.supported_output_modalities | join(",")),
        .rate_limit_tier
      ]
    | @tsv
  ' | column -t -s $'\t'
else
  echo "$response"
fi
