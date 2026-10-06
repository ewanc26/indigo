#ifndef INDIGO_UPDATE_SIG_H
#define INDIGO_UPDATE_SIG_H

/* The signature gate on the update manifest. Each release carries
 * `update.json.sig`, a detached Ed25519 signature over the exact bytes of
 * `update.json`, made in the release workflow. The updater checks it against
 * the key in update_key.h BEFORE it parses the manifest, and refuses a release
 * that has no signature or a wrong one. Wolfram does the cryptography
 * (wf_update_verify_signature); this file only holds Indigo's key and the
 * policy, so the decision is testable on the host. */

#include <stdbool.h>
#include <stddef.h>

/* 128 hex characters and a newline fit well inside this. */
#define INDIGO_UPDATE_SIGNATURE_MAX 256u
#define INDIGO_UPDATE_PUBLIC_KEY_LEN 32u

/* Decode INDIGO_UPDATE_PUBLIC_KEY_HEX. False if the constant is malformed. */
bool indigo_update_public_key(unsigned char out[INDIGO_UPDATE_PUBLIC_KEY_LEN]);

/* True only if `sig` (the contents of update.json.sig) is a valid signature of
 * `body` (the bytes of update.json) under `pk`. Anything else -- no signature,
 * malformed text, the wrong key, a changed byte -- is false. */
bool indigo_update_verify_manifest(const char *body, size_t body_len, const char *sig,
                                   size_t sig_len,
                                   const unsigned char pk[INDIGO_UPDATE_PUBLIC_KEY_LEN]);

#endif
