#!/usr/bin/env bash
# Build and check the self-update release assets (docs/UPDATE.md).
#
#   scripts/update-manifest.sh make   <version> <indigo.3dsx> <outdir>
#   scripts/update-manifest.sh verify <version> <dir>
#
# `make` writes into <outdir>:
#   indigo-<version>.3dsx          the build, under a versioned name
#   indigo-<version>.3dsx.sha256   sha256sum format
#   update.json                    the manifest in the wolfram#106 shape
# `verify` checks a directory of downloaded assets against each other and
# fails on any mismatch. release.sh runs `make`; the Release assets workflow
# runs `verify` on what GitHub actually serves.
set -euo pipefail

repo="ewanc26/indigo"
max_bytes=$((8 * 1024 * 1024))   # INDIGO_UPDATE_MAX_BYTES

die() { echo "update-manifest: $*" >&2; exit 1; }
need_version() { [[ "$1" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]] || die "version '$1' is not x.y.z"; }
sha() { sha256sum "$1" | cut -d' ' -f1; }

cmd="${1:-}"; shift || true
case "$cmd" in
make)
  version="${1:-}"; src="${2:-}"; out="${3:-}"
  need_version "$version"
  [[ -f "$src" ]] || die "no build at '$src'"
  mkdir -p "$out"
  name="indigo-$version.3dsx"
  cp "$src" "$out/$name"
  size=$(wc -c < "$out/$name" | tr -d ' ')
  (( size > 0 && size <= max_bytes )) || die "$name is $size bytes; the updater accepts 1..$max_bytes"
  digest=$(sha "$out/$name")
  (cd "$out" && sha256sum "$name" > "$name.sha256")
  # Notes stay a link: the release body is long and a manifest field that
  # does not fit is rejected, never truncated.
  printf '{"app":"indigo","version":"%s","notes":"https://github.com/%s/releases/tag/v%s","asset":{"name":"%s","url":"https://github.com/%s/releases/download/v%s/%s","size":%s,"sha256":"%s"},"signature":null}\n' \
    "$version" "$repo" "$version" "$name" "$repo" "$version" "$name" "$size" "$digest" > "$out/update.json"
  echo "update-manifest: $name $size bytes sha256 $digest"
  ;;
verify)
  version="${1:-}"; dir="${2:-}"
  need_version "$version"
  name="indigo-$version.3dsx"
  [[ -f "$dir/$name" && -f "$dir/$name.sha256" && -f "$dir/update.json" ]] || die "missing assets in '$dir'"
  (cd "$dir" && sha256sum -c --quiet "$name.sha256") || die "$name does not match $name.sha256"
  digest=$(sha "$dir/$name")
  size=$(wc -c < "$dir/$name" | tr -d ' ')
  python3 - "$dir/update.json" "$version" "$name" "$size" "$digest" "$repo" <<'PY' || die "update.json does not describe $name"
import json, sys
path, version, name, size, digest, repo = sys.argv[1:]
m = json.load(open(path))
want_url = f"https://github.com/{repo}/releases/download/v{version}/{name}"
a = m.get("asset", {})
checks = [
    m.get("app") == "indigo",
    m.get("version") == version,
    a.get("name") == name,
    a.get("url") == want_url,
    a.get("size") == int(size),
    a.get("sha256") == digest,
    "signature" in m,
]
sys.exit(0 if all(checks) else 1)
PY
  if [[ -f "$dir/indigo.3dsx" ]]; then
    cmp -s "$dir/indigo.3dsx" "$dir/$name" || die "indigo.3dsx and $name differ"
  fi
  echo "update-manifest: $dir verified for $version"
  ;;
*) echo "usage: $0 {make <version> <3dsx> <outdir>|verify <version> <dir>}" >&2; exit 2 ;;
esac
