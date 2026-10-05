#!/usr/bin/env bash
# Indigo's own checks. Branch, title, body, commit and merge-commit rules are
# the shared ones in Wolfram's tools/flow-check.sh (flow / conventions); this
# script holds what only Indigo needs. One script, so CI and a contributor run
# the same code.
#
#   scripts/check-flow.sh drift
#   scripts/check-flow.sh protocol
#   scripts/check-flow.sh release <version> [<ref>]
#
# Exit 0 when the check passes, 1 with a one-line reason per violation when it
# does not. scripts/check-flow-selftest.sh feeds each check a deliberate
# violation and requires it to fail, so a check cannot rot into a no-op.
set -uo pipefail

rc=0
bad() { echo "flow: $*" >&2; rc=1; }

# Every repository path the agent and user docs name must exist, so the docs
# cannot keep describing files that moved. Generated or deliberately absent
# paths are listed in scripts/flow-drift-allow.txt with the reason.
# The canonical flow block is Wolfram's text, copied verbatim, and names
# Wolfram's paths; it is checked by flow / drift, not here.
strip_canon() {
  awk '/<!-- flow:begin -->/{skip=1} !skip{print} /<!-- flow:end -->/{skip=0}' "$1"
}

check_drift() {
  local allow=scripts/flow-drift-allow.txt f p
  for f in AGENTS.md README.md CONTRIBUTING.md docs/*.md; do
    [[ -f "$f" ]] || continue
    while read -r p; do
      [[ -e "$p" ]] && continue
      grep -qxF "$p" <(sed 's/ *#.*//' "$allow") && continue
      bad "$f names '$p', which does not exist (fix the doc, or allow-list it with a reason in $allow)"
    done < <(strip_canon "$f" | grep -oE '`(src|scripts|tools|mk|docs|tests|romfs|\.github)/[A-Za-z0-9_./-]*`' | tr -d '`' | sort -u)
  done
  # make targets quoted in the docs must exist in the Makefiles.
  while read -r t; do
    grep -qE "^$t:" Makefile mk/*.mk || bad "docs run 'make $t' but no Makefile defines it"
  done < <(grep -ohE '`make [a-z][a-z0-9-]*`' AGENTS.md README.md CONTRIBUTING.md 2>/dev/null | sed 's/`make //;s/`//' | sort -u)
  # The newest CHANGELOG version must not be older than the newest tag.
  local top tag
  top=$(sed -n 's/^## \[\([0-9][0-9.]*\)\].*/\1/p' CHANGELOG.md | head -1)
  tag=$(git tag --list 'v[0-9]*' --sort=-v:refname 2>/dev/null | head -1 | sed 's/^v//')
  if [[ -n "$tag" && -n "$top" ]]; then
    [[ "$(printf '%s\n%s\n' "$top" "$tag" | sort -V | tail -1)" == "$top" ]] || \
      bad "CHANGELOG tops out at $top but tag v$tag exists"
  fi
}

# Protocol belongs in Wolfram. A raw lexicon method string in src/ is protocol
# knowledge; the few that exist are listed, with a reason, in
# scripts/flow-protocol-allow.txt as "<file> <prefix>".
check_protocol() {
  local allow=scripts/flow-protocol-allow.txt hit file lit
  while IFS=: read -r file lit; do
    lit=${lit//\"/}
    grep -qE "^$file +${lit%.*}" <(sed 's/ *#.*//' "$allow") && continue
    grep -qE "^$file +$lit" <(sed 's/ *#.*//' "$allow") && continue
    bad "$file holds the raw method string \"$lit\"; protocol belongs in Wolfram (or allow-list it with a reason in $allow)"
  done < <(grep -rnoE --include='*.c' --include='*.h' '"(com\.atproto|app\.bsky|uk\.ewancroft)\.[A-Za-z.]*"' src | sed -E 's/^([^:]+):[0-9]+:/\1:/')
}

# A tag is releasable only when its version has a CHANGELOG section, it is on
# main, and the CI jobs passed on that exact commit.
check_release() {
  local version="$1" ref="${2:-HEAD}"
  grep -q "^## \[$version\]" CHANGELOG.md || bad "no CHANGELOG section for $version"
  git fetch -q origin main 2>/dev/null || true
  git merge-base --is-ancestor "$(git rev-parse "$ref")" origin/main 2>/dev/null || \
    bad "$ref is not on origin/main"
  if [[ -n "${GITHUB_REPOSITORY:-}" && -n "${GH_TOKEN:-}" ]]; then
    local sha n
    sha=$(git rev-parse "$ref")
    n=$(gh api "repos/$GITHUB_REPOSITORY/commits/$sha/check-runs" \
      --jq '[.check_runs[] | select(.name=="CI gate" and .conclusion=="success")] | length' 2>/dev/null || echo 0)
    [[ "$n" -ge 1 ]] || bad "no successful 'CI gate' run on ${sha:0:9}"
  fi
}

cmd="${1:-}"; shift || true
case "$cmd" in
  drift)   check_drift ;;
  protocol) check_protocol ;;
  release) check_release "${1:-}" "${2:-HEAD}" ;;
  *) echo "usage: $0 {drift|protocol|release} ..." >&2; exit 2 ;;
esac
exit $rc
