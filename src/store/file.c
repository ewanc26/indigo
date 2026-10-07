#include "store/file.h"

#include <stdio.h>

#define PATH_MAX_STORE 300

indigo_store_status
indigo_file_write_atomic(const char *path, const void *data, size_t len)
{
    char tmp[PATH_MAX_STORE];
    FILE *f;
    int ok;

    if (!path || !data || snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int) sizeof tmp) {
        return INDIGO_STORE_IO;
    }
    f = fopen(tmp, "wb");
    if (!f) {
        return INDIGO_STORE_IO;
    }
    ok = fwrite(data, 1, len, f) == len;
    ok = (fclose(f) == 0) && ok;
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
indigo_file_read(const char *path, void *buf, size_t cap, size_t *len)
{
    FILE *f;
    int err;

    if (len) {
        *len = 0;
    }
    if (!path || !buf || !len) {
        return INDIGO_STORE_IO;
    }
    f = fopen(path, "rb");
    if (!f) {
        return INDIGO_STORE_MISSING;
    }
    *len = fread(buf, 1, cap, f);
    err = ferror(f) != 0;
    fclose(f);
    if (err) {
        *len = 0;
        return INDIGO_STORE_IO;
    }
    return INDIGO_STORE_OK;
}

void
indigo_file_set_aside(const char *path)
{
    char bad[PATH_MAX_STORE];

    if (path && snprintf(bad, sizeof bad, "%s.bad", path) < (int) sizeof bad) {
        remove(bad);
        rename(path, bad);
    }
}

indigo_store_status
indigo_file_remove(const char *path)
{
    if (path && remove(path) != 0) {
        FILE *f = fopen(path, "rb");

        if (f) {
            fclose(f);
            return INDIGO_STORE_IO;
        }
    }
    return INDIGO_STORE_OK;
}
