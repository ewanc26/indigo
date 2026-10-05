#!/usr/bin/env bash
# Cut an Indigo release by tagging main. CI does the rest.
#
#   scripts/release.sh [--dry-run] <version>      e.g. scripts/release.sh 0.6.0
#
# Refuses unless: you are on a clean main identical to origin/main, the tag
# does not exist, CHANGELOG.md has a non-empty "## [<version>]" section (it
# goes in through a release PR like any other change), the host checks pass,
# and `CI gate` is green on that exact commit.
#
# It then creates an annotated tag and pushes it. .github/workflows/release.yml
# picks the tag up: it re-checks all of the above, builds the .3dsx from the
# tag in the devkitARM container against the Wolfram release in wolfram.ref
# (so `git describe` stamps the binary with the release it is), writes the
# update assets with scripts/update-manifest.sh, publishes the GitHub release
# with the CHANGELOG section as its notes, and verifies what GitHub serves.
#
# Tags are never moved or re-cut. A wrong release is fixed by a new patch.
set -euo pipefail

dry_run=0
if [[ "${1:-}" == "--dry-run" ]]; then
  dry_run=1
  shift
fi

version="${1:-}"
if [[ ! "$version" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
  echo "usage: $0 [--dry-run] <MAJOR.MINOR.PATCH>" >&2
  exit 2
fi
tag="v$version"

cd "$(git rev-parse --show-toplevel)"

fail() { echo "release: $*" >&2; exit 1; }

branch="$(git rev-parse --abbrev-ref HEAD)"
[[ "$branch" == "main" ]] || fail "must be on main (currently on $branch)"
[[ -z "$(git status --porcelain)" ]] || fail "working tree is not clean"

git fetch origin --tags --prune
local_sha="$(git rev-parse main)"
remote_sha="$(git rev-parse origin/main)"
[[ "$local_sha" == "$remote_sha" ]] || \
  fail "local main ($local_sha) differs from origin/main ($remote_sha); sync first"
git rev-parse -q --verify "refs/tags/$tag" >/dev/null && fail "tag $tag already exists; cut a new patch instead"

scripts/release-notes.sh "$version" >/dev/null || fail "CHANGELOG.md has no '## [$version]' section with content"

echo "release: host checks"
make test warnings

echo "release: CI gate on ${local_sha:0:9}"
scripts/check-flow.sh release "$version" "$local_sha" || fail "not releasable"

if (( dry_run )); then
  echo "release: dry run complete; $tag would be tagged at ${local_sha:0:9}, nothing was pushed"
  exit 0
fi

git tag -a "$tag" -m "Indigo $version" "$local_sha"
git push origin "$tag"
echo "release: pushed $tag; .github/workflows/release.yml builds and publishes it"
