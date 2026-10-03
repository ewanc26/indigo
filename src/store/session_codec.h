#ifndef INDIGO_SESSION_CODEC_H
#define INDIGO_SESSION_CODEC_H

#include <stdbool.h>
#include <stddef.h>

#define INDIGO_SESSION_FORMAT_VERSION 1
#define INDIGO_SESSION_FIELD_MAX 2048
#define INDIGO_SESSION_FILE_MAX 12288

/* Everything needed to resume a session, and nothing else. */
typedef struct {
    char service[256];
    char handle[256];
    char did[256];
    char pds_url[256];
    char access_jwt[INDIGO_SESSION_FIELD_MAX];
    char refresh_jwt[INDIGO_SESSION_FIELD_MAX];
} indigo_saved_session;

typedef enum {
    INDIGO_CODEC_OK = 0,
    INDIGO_CODEC_EMPTY,
    INDIGO_CODEC_BAD_VERSION,
    INDIGO_CODEC_CORRUPT,
    INDIGO_CODEC_INCOMPLETE,
    INDIGO_CODEC_TOO_BIG,
} indigo_codec_status;

/* Versioned "key=value" lines. Refuses values containing newlines. */
indigo_codec_status indigo_session_encode(const indigo_saved_session *s,
                                          char *out, size_t cap, size_t *len);

/* Tolerates missing, empty, truncated and garbage input. */
indigo_codec_status indigo_session_decode(const char *data, size_t len,
                                          indigo_saved_session *out);

void indigo_session_wipe(indigo_saved_session *s);

#endif
