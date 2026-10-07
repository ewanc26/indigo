#include "store/settings_store.h"

#include "store/file.h"

indigo_store_status
indigo_settings_store_save(const char *path, const indigo_settings *s)
{
    static char buf[INDIGO_SETTINGS_FILE_MAX];
    size_t len = 0;

    if (indigo_settings_encode(s, buf, sizeof buf, &len) != INDIGO_CODEC_OK) {
        return INDIGO_STORE_IO;
    }
    return indigo_file_write_atomic(path, buf, len);
}

indigo_store_status
indigo_settings_store_load(const char *path, indigo_settings *out)
{
    static char buf[INDIGO_SETTINGS_FILE_MAX + 1];
    size_t n = 0;
    indigo_store_status st;

    indigo_settings_defaults(out);
    st = indigo_file_read(path, buf, sizeof buf, &n);
    if (st != INDIGO_STORE_OK) {
        return st;
    }
    if (indigo_settings_decode(buf, n, out) == INDIGO_CODEC_OK) {
        return INDIGO_STORE_OK;
    }
    /* Keep the damaged file for inspection rather than silently discarding
     * it. Unlike the session, settings are not worth failing a boot over, so
     * the caller keeps running on the defaults decode already left behind. */
    indigo_file_set_aside(path);
    return INDIGO_STORE_UNREADABLE;
}
