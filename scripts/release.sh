#!/usr/bin/env bash
# Cut an Indigo release: tag, build, GitHub release with the .3dsx attached.
#
#   scripts/release.sh [--dry-run] <version>      e.g. scripts/release.sh 0.1.0
#
# Refuses to run unless the working tree is clean, you are on main, and local
# main is identical to origin/main. CHANGELOG.md must already hold a
# "## [<version>]" section; its body becomes the release notes.
set -euo pipefail

dry_run=0
if [[ "${1:-}" == "--dry-run" ]]; then
  dry_run=1
  shift
fi

version="${1:-}"
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
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

git rev-parse -q --verify "refs/tags/$tag" >/dev/null && fail "tag $tag already exists"

notes="$(awk -v v="$version" '
  $0 ~ "^## \\[" v "\\]" {found=1; next}
  found && /^## \[/ {exit}
  found {print}
' CHANGELOG.md)"
[[ -n "${notes//[[:space:]]/}" ]] || fail "CHANGELOG.md has no '## [$version]' section with content"

echo "release: host checks"
make test warnings

echo "release: 3DS build"
make clean
make
[[ -f indigo.3dsx ]] || fail "make did not produce indigo.3dsx"

if (( dry_run )); then
  echo "release: dry run complete; would tag $tag at $local_sha and publish indigo.3dsx"
  exit 0
fi

git tag -a "$tag" -m "Indigo $version" "$local_sha"
git push origin "$tag"
gh release create "$tag" indigo.3dsx --title "Indigo $version" --notes "$notes" --verify-tag
echo "release: published $tag"
