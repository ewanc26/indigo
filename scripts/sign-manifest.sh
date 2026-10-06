#!/usr/bin/env bash
# Sign or verify a release's update.json with Indigo's Ed25519 release key.
#
#   UPDATE_SIGNING_KEY=<PEM> scripts/sign-manifest.sh sign   update.json update.json.sig
#                            scripts/sign-manifest.sh verify update.json update.json.sig
#
# `sign` reads the private key from the environment variable UPDATE_SIGNING_KEY
# (in CI, the repository secret of that name), writes it to a 0600 temp file that
# is removed straight away, never prints it, and checks the signature it made
# against the public key committed in src/update/update_key.h before it writes
# the result: a wrong or rotated secret fails here instead of publishing a
# signature the console would refuse. The signature file is the format
# wolfram/update.h reads: 128 hex characters and a newline.
# `verify` needs no secret and uses the same committed key.
set -euo pipefail
key_header="$(dirname "$0")/../src/update/update_key.h"

public_pem() { # writes a PEM public key for the committed hex key to $1
	local pub
	pub="$(sed -n 's/.*"\([0-9a-f]\{64\}\)".*/\1/p' "$key_header")"
	[ "${#pub}" = 64 ] || { echo "sign-manifest: no public key in $key_header" >&2; exit 1; }
	# An Ed25519 SubjectPublicKeyInfo is this fixed prefix and the 32 key bytes.
	{ printf '302a300506032b6570032100'; printf '%s' "$pub"; } | xxd -r -p >"$1.der"
	openssl pkey -pubin -inform DER -in "$1.der" -out "$1"
}

verify_with_committed_key() { # json sigfile(hex)
	local work
	work="$(mktemp -d)"
	trap 'rm -rf "$work"' RETURN
	public_pem "$work/pub.pem"
	tr -d ' \n\r' <"$2" | xxd -r -p >"$work/sig.bin"
	[ "$(wc -c <"$work/sig.bin" | tr -d ' ')" = 64 ] || { echo "sign-manifest: $2 is not 128 hex characters" >&2; return 1; }
	openssl pkeyutl -verify -rawin -pubin -inkey "$work/pub.pem" -in "$1" -sigfile "$work/sig.bin" >/dev/null
}

cmd="${1:-}"
case "$cmd" in
sign)
	json="${2:?usage: sign-manifest.sh sign update.json update.json.sig}"
	out="${3:?usage: sign-manifest.sh sign update.json update.json.sig}"
	[ -n "${UPDATE_SIGNING_KEY:-}" ] || { echo "sign-manifest: UPDATE_SIGNING_KEY is not set" >&2; exit 1; }
	umask 077
	work="$(mktemp -d)"
	trap 'rm -rf "$work"' EXIT
	printf '%s\n' "$UPDATE_SIGNING_KEY" >"$work/key.pem"
	openssl pkeyutl -sign -rawin -inkey "$work/key.pem" -in "$json" -out "$work/sig.bin"
	rm -f "$work/key.pem"
	xxd -p -c 256 "$work/sig.bin" >"$work/sig.hex"
	verify_with_committed_key "$json" "$work/sig.hex" ||
		{ echo "sign-manifest: the signature does not verify against $key_header; refusing to write it" >&2; exit 1; }
	mv "$work/sig.hex" "$out"
	echo "sign-manifest: signed $json and verified it against $key_header"
	;;
verify)
	json="${2:?usage: sign-manifest.sh verify update.json update.json.sig}"
	sig="${3:?usage: sign-manifest.sh verify update.json update.json.sig}"
	[ -f "$sig" ] || { echo "sign-manifest: $sig is missing; an unsigned release is refused" >&2; exit 1; }
	verify_with_committed_key "$json" "$sig" || { echo "sign-manifest: $sig does not verify against $key_header" >&2; exit 1; }
	echo "sign-manifest: $sig verifies against $key_header"
	;;
*)
	echo "usage: sign-manifest.sh sign|verify update.json update.json.sig" >&2
	exit 2
	;;
esac
