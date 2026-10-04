#include "store/settings_store.h"

#include <stdio.h>
#include <string.h>

indigo_store_status
indigo_settings_store_save(const char *path, const indigo_settings *s)
{
    static char buf[INDIGO_SETTINGS_FILE_MAX];
    char tmp[300];
    size_t len = 0;

    if (indigo_settings_encode(s, buf, sizeof buf, &len) != INDIGO_CODEC_OK ||
        snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int) sizeof tmp) {
        return INDIGO_STORE_IO;
    }

    FILE *f = fopen(tmp, "wb");
    if (!f) {
        return INDIGO_STORE_IO;
    }
    bool ok = fwrite(buf, 1, len, f) == len;
    ok = (fclose(f) == 0) && ok;
    memset(buf, 0, sizeof buf);
    if (!ok) {
        remove(tmp);
        return INDIGO_STORE_IO;
    }
    /* FAT rename will not overwrite on every libc, so clear the target first. */
    remove(path);
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return INDIGO_STORE_IO;
    }
    return INDIGO_STORE_OK;
}

indigo_store_status
indigo_settings_store_load(const char *path, indigo_settings *out)
{
    static char buf[INDIGO_SETTINGS_FILE_MAX + 1];
    FILE *f;

    indigo_settings_defaults(out);
    f = fopen(path, "rb");
    if (!f) {
        return INDIGO_STORE_MISSING;
    }
    size_t n = fread(buf, 1, sizeof buf, f);
    bool err = ferror(f) != 0;
    fclose(f);
    if (err) {
        memset(buf, 0, sizeof buf);
        return INDIGO_STORE_IO;
    }

    bool ok = indigo_settings_decode(buf, n, out) == INDIGO_CODEC_OK;

    memset(buf, 0, sizeof buf);
    if (ok) {
        return INDIGO_STORE_OK;
    }

    /* Keep the damaged file for inspection rather than silently discarding
     * it. Unlike the session, settings are not worth failing a boot over, so
     * the caller keeps running on the defaults decode already left behind. */
    char bad[300];
    if (snprintf(bad, sizeof bad, "%s.bad", path) < (int) sizeof bad) {
        remove(bad);
        rename(path, bad);
    }
    return INDIGO_STORE_UNREADABLE;
}

indigo_store_status
indigo_settings_store_clear(const char *path)
{
    if (remove(path) != 0) {
        FILE *f = fopen(path, "rb");

        if (f) {
            fclose(f);
            return INDIGO_STORE_IO;
        }
    }
    return INDIGO_STORE_OK;
}
