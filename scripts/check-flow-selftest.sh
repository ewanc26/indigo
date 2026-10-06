#!/usr/bin/env bash
# Prove every Indigo check can fail (the shared branch, title, body and
# commit rules are Wolfram's, with their own self-test). Each case is a deliberate violation that
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

# Universal-DB listing: a hand edit, and a filter that would offer the
# versioned asset to Universal-Updater, must both fail.
expect pass udb-current python3 scripts/udb-listing.py --check
cp meta/universal-db/indigo.json "$tmp/udb.json"
sed -i 's/"app"/"game"/' meta/universal-db/indigo.json
expect fail udb-hand-edited python3 scripts/udb-listing.py --check
cp "$tmp/udb.json" meta/universal-db/indigo.json
cp scripts/udb-listing.py "$tmp/udb-listing.py"
sed -i 's|^DOWNLOAD_FILTER = .*|DOWNLOAD_FILTER = r"\\.3dsx$"|' scripts/udb-listing.py
expect fail udb-filter-too-wide python3 scripts/udb-listing.py --check
cp "$tmp/udb-listing.py" scripts/udb-listing.py

expect fail release-no-changelog env RELEASE_SKIP_GATE=1 $c release 99.0.0
top=$(sed -n 's/^## \[\([0-9][0-9.]*\)\].*/\1/p' CHANGELOG.md | head -1)
expect pass release-notes-present scripts/release-notes.sh "$top"
expect fail release-notes-absent scripts/release-notes.sh 99.0.0
# A commit that is not on origin/main is refused even with a CHANGELOG section.
r="$tmp/relrepo"; git init -q -b main "$r"; mkdir -p "$r/scripts"
cp CHANGELOG.md "$r/"; cp scripts/release-notes.sh scripts/check-flow.sh "$r/scripts/"
git -C "$r" add -A; git -C "$r" -c user.name=t -c user.email=t@t commit -q -m 'chore: base'
git clone -q --bare "$r" "$tmp/relorigin.git"; git -C "$r" remote add origin "$tmp/relorigin.git"
git -C "$r" fetch -q origin
expect pass release-on-main env RELEASE_SKIP_GATE=1 bash -c "cd $r && scripts/check-flow.sh release $top HEAD"
git -C "$r" -c user.name=t -c user.email=t@t commit -q --allow-empty -m 'fix: local only'
expect fail release-not-on-main env RELEASE_SKIP_GATE=1 bash -c "cd $r && scripts/check-flow.sh release $top HEAD"
exit $fails
