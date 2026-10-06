#include "update/update_sig.h"

#include "update/update_key.h"

#include <string.h>
#include <wolfram/update.h>

bool
indigo_update_public_key(unsigned char out[INDIGO_UPDATE_PUBLIC_KEY_LEN])
{
    /* 64 hex characters decode to 32 bytes: the same shape as a SHA-256. */
    return strlen(INDIGO_UPDATE_PUBLIC_KEY_HEX) == 64 &&
           wf_sha256_from_hex(INDIGO_UPDATE_PUBLIC_KEY_HEX, 64, out) == WF_OK;
}

bool
indigo_update_verify_manifest(const char *body, size_t body_len, const char *sig,
                              size_t sig_len,
                              const unsigned char pk[INDIGO_UPDATE_PUBLIC_KEY_LEN])
{
    if (!body || body_len == 0 || !sig || !pk) {
        return false;
    }
    return wf_update_verify_signature(body, body_len, sig, sig_len, pk) == WF_OK;
}
