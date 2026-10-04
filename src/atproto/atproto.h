#ifndef INDIGO_ATPROTO_H
#define INDIGO_ATPROTO_H

#include <stdbool.h>

/* The bundled Mozilla trust store, in ROMFS. Every client that verifies a
 * server needs this, including the media loader's own token-less one, so it is
 * named here rather than spelled out per module. */
#define INDIGO_CA_BUNDLE_PATH "romfs:/cacert.pem"

bool indigo_atproto_init(void);
void indigo_atproto_shutdown(void);
bool indigo_atproto_available(void);

#endif
