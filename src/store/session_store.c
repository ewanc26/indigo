#include "store/session_store.h"

#include <stdio.h>
#include <string.h>

indigo_store_status
indigo_session_store_save(const char *path, const indigo_saved_session *s)
{
    static char buf[INDIGO_SESSION_FILE_MAX];
    char tmp[300];
    size_t len = 0;

    if (indigo_session_encode(s, buf, sizeof buf, &len) != INDIGO_CODEC_OK ||
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
indigo_session_store_load(const char *path, indigo_saved_session *out)
{
    static char buf[INDIGO_SESSION_FILE_MAX + 1];
    FILE *f = fopen(path, "rb");

    memset(out, 0, sizeof *out);
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

    indigo_codec_status st = indigo_session_decode(buf, n, out);
    memset(buf, 0, sizeof buf);
    if (st == INDIGO_CODEC_OK) {
        return INDIGO_STORE_OK;
    }

    /* Keep the bad file for inspection rather than silently discarding it. */
    char bad[300];
    if (snprintf(bad, sizeof bad, "%s.bad", path) < (int) sizeof bad) {
        remove(bad);
        rename(path, bad);
    }
    return INDIGO_STORE_UNREADABLE;
}

indigo_store_status
indigo_session_store_clear(const char *path)
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
