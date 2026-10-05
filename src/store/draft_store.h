#ifndef INDIGO_DRAFT_STORE_H
#define INDIGO_DRAFT_STORE_H

#include "store/store_status.h"

#include <stddef.h>

/* The unsent compose text, kept on the card so closing the app (or losing the
 * battery) does not cost anyone a post they were writing. It is the text only:
 * what a draft was replying to is not worth restoring into a screen the person
 * did not open, and the text is what cannot be recovered.
 *
 * File format, version 1:
 *     indigo-draft 1\n
 *     len=<bytes>\n
 *     <exactly that many bytes of UTF-8>\n
 *     end\n
 * The length and the end marker are what make a truncated write detectable
 * instead of silently loading half a post. */

/* Pure codec, host-testable. decode copies into out (cap bytes, always
 * NUL-terminated) and touches it only on INDIGO_CODEC_OK. */
indigo_codec_status indigo_draft_encode(const char *text, char *buf, size_t cap,
                                        size_t *len);
indigo_codec_status indigo_draft_decode(const char *buf, size_t len, char *out,
                                        size_t cap);

/* An empty text removes the file: there is nothing to keep. */
indigo_store_status indigo_draft_store_save(const char *path, const char *text);

/* out is left empty unless the file held a valid draft. A damaged file is
 * moved aside to `.bad`, never deleted. */
indigo_store_status indigo_draft_store_load(const char *path, char *out, size_t cap);

#endif
