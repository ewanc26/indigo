#!/usr/bin/env bash
# Print the body of CHANGELOG.md's "## [<version>]" section; fail if it is
# missing or empty. The release notes are exactly this text.
set -euo pipefail
version="${1:?usage: release-notes.sh <version>}"
cd "$(git rev-parse --show-toplevel)"
notes="$(awk -v v="$version" '
  index($0, "## [" v "]") == 1 {found=1; next}
  found && /^## \[/ {exit}
  found {print}
' CHANGELOG.md)"
[[ -n "${notes//[[:space:]]/}" ]] || { echo "release-notes: no '## [$version]' section with content" >&2; exit 1; }
printf '%s\n' "$notes"
