#include "store/session_store.h"

#include "store/file.h"

#include <string.h>

indigo_store_status
indigo_session_store_save(const char *path, const indigo_saved_session *s)
{
    static char buf[INDIGO_SESSION_FILE_MAX];
    size_t len = 0;
    indigo_store_status st = INDIGO_STORE_IO;

    if (indigo_session_encode(s, buf, sizeof buf, &len) == INDIGO_CODEC_OK) {
        st = indigo_file_write_atomic(path, buf, len);
    }
    /* The buffer held tokens. */
    memset(buf, 0, sizeof buf);
    return st;
}

indigo_store_status
indigo_session_store_load(const char *path, indigo_saved_session *out)
{
    static char buf[INDIGO_SESSION_FILE_MAX + 1];
    size_t n = 0;
    indigo_store_status st;
    indigo_codec_status decoded;

    memset(out, 0, sizeof *out);
    st = indigo_file_read(path, buf, sizeof buf, &n);
    if (st != INDIGO_STORE_OK) {
        memset(buf, 0, sizeof buf);
        return st;
    }
    decoded = indigo_session_decode(buf, n, out);
    memset(buf, 0, sizeof buf);
    if (decoded == INDIGO_CODEC_OK) {
        return INDIGO_STORE_OK;
    }
    /* Keep the bad file for inspection rather than silently discarding it. */
    indigo_file_set_aside(path);
    return INDIGO_STORE_UNREADABLE;
}

indigo_store_status
indigo_session_store_clear(const char *path)
{
    return indigo_file_remove(path);
}
