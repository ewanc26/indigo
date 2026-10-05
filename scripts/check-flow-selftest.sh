#!/usr/bin/env bash
# Prove every flow check can fail. Each case is a deliberate violation that
# must be rejected, plus a conforming input that must be accepted.
set -u
cd "$(git rev-parse --show-toplevel)"
c=scripts/check-flow.sh
fails=0
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
expect() { # expect pass|fail <label> <command...>
  local want="$1" label="$2"; shift 2
  if "$@" >/dev/null 2>&1; then got=pass; else got=fail; fi
  if [[ "$got" == "$want" ]]; then echo "ok   $label ($want)"; else echo "FAIL $label: wanted $want, got $got"; fails=1; fi
}

expect pass branch-good $c branch feat/image-viewer
expect fail branch-bad  $c branch Ewan-patch-1
expect fail branch-main $c branch main
expect pass title-good  $c title 'fix(ui): stop clipping the status line'
expect fail title-bad   $c title 'Updated stuff'
expect fail title-type  $c title 'wip: half done thing'

printf '## Summary\nx\n\n## Verification\nhost only: make test\n' > "$tmp/good.md"
printf 'just a sentence\n' > "$tmp/bad.md"
printf '## Summary\nx\n' > "$tmp/nover.md"
: > "$tmp/empty.md"
expect pass body-good   $c body "$tmp/good.md"
expect fail body-prose  $c body "$tmp/bad.md"
expect fail body-nover  $c body "$tmp/nover.md"
expect fail body-empty  $c body "$tmp/empty.md"

# Commits: build a throwaway repo so the check sees real history.
r="$tmp/repo"; git init -q "$r"
git -C "$r" -c user.name=t -c user.email=t@t commit -q --allow-empty -m 'chore: base'
git -C "$r" -c user.name=t -c user.email=t@t commit -q --allow-empty -m 'fix(ui): good one'
expect pass commits-good bash -c "cd $r && $OLDPWD/$c commits HEAD~1..HEAD"
git -C "$r" -c user.name=t -c user.email=t@t commit -q --allow-empty -m 'did a thing'
expect fail commits-bad bash -c "cd $r && $OLDPWD/$c commits HEAD~1..HEAD"

# Drift: a doc naming a missing path, and a missing make target, must fail.
expect pass drift-clean $c drift
cp AGENTS.md "$tmp/AGENTS.md.bak"
echo 'See `src/app/does_not_exist.c`.' >> AGENTS.md
expect fail drift-path $c drift
cp "$tmp/AGENTS.md.bak" AGENTS.md
echo 'Run `make nonexistent-target`.' >> AGENTS.md
expect fail drift-make $c drift
cp "$tmp/AGENTS.md.bak" AGENTS.md

# Protocol guard: a raw lexicon method in src/ must be rejected.
expect pass protocol-clean $c protocol
printf '#define X "app.bsky.feed.getTimeline"\n' > src/util/zz_selftest.h
expect fail protocol-raw-nsid $c protocol
rm -f src/util/zz_selftest.h

expect fail release-no-changelog $c release 99.0.0
exit $fails
