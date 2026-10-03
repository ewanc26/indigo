#ifndef INDIGO_SESSION_STORE_H
#define INDIGO_SESSION_STORE_H

#include "store/session_codec.h"

typedef enum {
    INDIGO_STORE_OK = 0,
    INDIGO_STORE_MISSING,
    INDIGO_STORE_IO,
    INDIGO_STORE_UNREADABLE, /* present but corrupt; moved aside, not deleted */
} indigo_store_status;

/*
 * The session file holds a refresh token in plain text, like other 3DS
 * homebrew: the SD card has no permissions to enforce and an obfuscation key
 * stored beside the file would only imply protection that is not there. It is
 * written atomically, holds the minimum needed to resume, and is deleted by
 * sign-out.
 */
indigo_store_status indigo_session_store_save(const char *path,
                                              const indigo_saved_session *s);
indigo_store_status indigo_session_store_load(const char *path,
                                              indigo_saved_session *out);
indigo_store_status indigo_session_store_clear(const char *path);

#endif
