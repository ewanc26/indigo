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

# Merge commits: a merged side branch in the range must be rejected.
expect pass merges-linear bash -c "cd $r && $OLDPWD/$c merges HEAD~2..HEAD"
git -C "$r" checkout -q -b side HEAD~1
git -C "$r" -c user.name=t -c user.email=t@t commit -q --allow-empty -m 'fix: side'
git -C "$r" checkout -q -
git -C "$r" -c user.name=t -c user.email=t@t merge -q --no-ff --no-edit side
expect fail merges-merge-commit bash -c "cd $r && $OLDPWD/$c merges HEAD~3..HEAD"

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

# Update assets: a consistent set verifies; a tampered build, a manifest that
# names another host, and a bad version must not.
u=scripts/update-manifest.sh
head -c 4096 /dev/urandom > "$tmp/indigo.3dsx"
expect pass update-make $u make 1.2.3 "$tmp/indigo.3dsx" "$tmp/rel"
cp "$tmp/indigo.3dsx" "$tmp/rel/indigo.3dsx"
expect pass update-verify $u verify 1.2.3 "$tmp/rel"
cp -r "$tmp/rel" "$tmp/tamper"; printf x >> "$tmp/tamper/indigo-1.2.3.3dsx"
expect fail update-tampered-build $u verify 1.2.3 "$tmp/tamper"
cp -r "$tmp/rel" "$tmp/host"; sed -i 's#https://github.com/#https://example.com/#g' "$tmp/host/update.json"
expect fail update-foreign-url $u verify 1.2.3 "$tmp/host"
cp -r "$tmp/rel" "$tmp/stale"; printf y >> "$tmp/stale/indigo.3dsx"
expect fail update-stale-plain-asset $u verify 1.2.3 "$tmp/stale"
expect fail update-bad-version $u make v1.2.3 "$tmp/indigo.3dsx" "$tmp/rel2"
expect fail update-wrong-version $u verify 1.2.4 "$tmp/rel"

# Generated art: an edited icon must be caught.
expect pass art-current python3 tools/gen_logo.py --check
cp assets/icon.png "$tmp/icon.png"
printf x >> assets/icon.png
expect fail art-hand-edited python3 tools/gen_logo.py --check
cp "$tmp/icon.png" assets/icon.png

expect fail release-no-changelog $c release 99.0.0
exit $fails
