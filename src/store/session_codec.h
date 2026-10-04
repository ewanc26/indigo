#ifndef INDIGO_SESSION_CODEC_H
#define INDIGO_SESSION_CODEC_H

#include "store/store_status.h"

#include <stdbool.h>
#include <stddef.h>

#define INDIGO_SESSION_FORMAT_VERSION 1
#define INDIGO_SESSION_PAYLOAD_MAX 8192
#define INDIGO_SESSION_FILE_MAX 12288

/* The file envelope only: which service the user signed in to, and Wolfram's
 * own serialised session (wf_session_data_to_json) as an opaque payload.
 * Indigo never looks inside the payload. */
typedef struct {
    char service[256];
    char session[INDIGO_SESSION_PAYLOAD_MAX];
} indigo_saved_session;

/* Versioned "key=value" lines. Refuses values containing newlines. */
indigo_codec_status indigo_session_encode(const indigo_saved_session *s,
                                          char *out, size_t cap, size_t *len);

/* Tolerates missing, empty, truncated and garbage input. */
indigo_codec_status indigo_session_decode(const char *data, size_t len,
                                          indigo_saved_session *out);

void indigo_session_wipe(indigo_saved_session *s);

#endif
