/* Timeline cache store (indigo #13). The codec turns a page into bytes and back;
 * this file moves those bytes to and from the SD card. The path is passed in
 * by the caller, which owns the application's directory. */
#include "store/cache_store.h"

#include "store/file.h"

indigo_store_status
indigo_cache_store_save(const char *path, const indigo_cache_timeline *t)
{
    static char buf[INDIGO_CACHE_FILE_MAX];
    size_t len = 0;

    if (indigo_cache_encode(t, buf, sizeof buf, &len) != INDIGO_CODEC_OK) {
        return INDIGO_STORE_IO;
    }
    return indigo_file_write_atomic(path, buf, len);
}

indigo_store_status
indigo_cache_store_load(const char *path, indigo_cache_timeline *out)
{
    /* One byte past the largest valid file, so a longer file reads as too big
     * rather than being truncated into something that might decode. */
    static char buf[INDIGO_CACHE_FILE_MAX + 1];
    size_t n = 0;
    indigo_store_status st;

    indigo_cache_timeline_clear(out);
    st = indigo_file_read(path, buf, sizeof buf, &n);
    if (st != INDIGO_STORE_OK) {
        return st;
    }
    if (indigo_cache_decode(buf, n, out) == INDIGO_CODEC_OK) {
        return INDIGO_STORE_OK;
    }
    /* A cache is cheap to refill, but the file is still moved aside, not
     * deleted, in case it is a damaged copy of something worth a look. */
    indigo_file_set_aside(path);
    return INDIGO_STORE_UNREADABLE;
}
