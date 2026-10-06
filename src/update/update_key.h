#ifndef INDIGO_UPDATE_KEY_H
#define INDIGO_UPDATE_KEY_H

/* The public half of Indigo's release signing key: 32 bytes of Ed25519 as 64
 * hex characters. The private half exists only as the GitHub Actions secret
 * UPDATE_SIGNING_KEY, which the release workflow uses to sign update.json
 * (scripts/sign-manifest.sh). Replacing this key means shipping a build that
 * carries the new one, signed by the old one; a client trusts only the key it
 * has. */
#define INDIGO_UPDATE_PUBLIC_KEY_HEX \
    "92a92ecc4036136c26aaca4a7c667875650ded87c39edc7d9fadba1f5e65256d"

#endif
