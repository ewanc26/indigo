#!/usr/bin/env bash
# Open Indigo's Universal-DB pull request, in my name. I run this, not an agent:
# the listing is a submission to someone else's project (docs/UNIVERSAL-DB.md).
#
#   scripts/udb-submit.sh            fork, commit, push, open the PR page
#   scripts/udb-submit.sh --dry-run  build the files into a scratch tree only
#
# Needs: gh logged in as me, git, python3, a C compiler (for the screenshots).
# What it does:
#   1. checks meta/universal-db/indigo.json is current (scripts/udb-listing.py);
#   2. renders the screenshots with the host snapshot renderer;
#   3. forks Universal-Team/db (or reuses my fork), syncs it, and makes a branch
#      add-indigo with source/apps/indigo.json and
#      docs/assets/images/screenshots/indigo/*.png;
#   4. pushes the branch to my fork and opens GitHub's "create pull request"
#      page with the title filled in.
# The description is left empty on purpose. Universal-DB does not accept PR or
# issue text written by an LLM, so that sentence or two is mine to write.
set -euo pipefail

dry_run=0
[[ "${1:-}" == "--dry-run" ]] && dry_run=1

cd "$(git rev-parse --show-toplevel)"
upstream="Universal-Team/db"
branch="add-indigo"

python3 scripts/udb-listing.py --check
make snapshots >/dev/null

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/source/apps" "$stage/docs/assets/images/screenshots/indigo"
cp meta/universal-db/indigo.json "$stage/source/apps/indigo.json"
while read -r src dst; do
  cp "build-host/snapshots/$src" "$stage/docs/assets/images/screenshots/indigo/$dst"
done < <(python3 scripts/udb-listing.py --screenshots)

if (( dry_run )); then
  (cd "$stage" && find . -type f | sort)
  echo "udb-submit: dry run; nothing was forked, pushed or opened"
  exit 0
fi

command -v gh >/dev/null || { echo "udb-submit: needs the GitHub CLI, logged in as you" >&2; exit 1; }
me=$(gh api user --jq .login)
gh repo fork "$upstream" --clone=false >/dev/null 2>&1 || true
gh repo sync "$me/db" --source "$upstream" >/dev/null
work=$(mktemp -d)
git clone -q --depth 1 "https://github.com/$me/db.git" "$work/db"
cd "$work/db"
git checkout -q -b "$branch"
cp -R "$stage/." .
git add source/apps/indigo.json docs/assets/images/screenshots/indigo
git commit -q -m "Add Indigo"
git push -q -u origin "$branch"
gh pr create --repo "$upstream" --head "$me:$branch" --title "Add Indigo" --web
echo "udb-submit: the pull request page is open; write the description yourself and submit it"
