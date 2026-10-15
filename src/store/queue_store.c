/* Offline queue store: the codec turns the queue into bytes and back, and this
 * file moves those bytes to and from the SD card through the shared helpers. */
#include "store/queue_store.h"

#include "store/file.h"
#include "store/queue_codec.h"

#include <stdio.h>

/* Worst case: each post escaped to twice its text, plus a timestamp and the
 * line separators, plus the header and the end line. */
#define QUEUE_FILE_MAX \
    (INDIGO_QUEUE_MAX * (2 * INDIGO_QUEUE_TEXT_MAX + 64) + 64)

indigo_store_status
indigo_queue_store_save(const char *path, const indigo_post_queue *q)
{
    static char buf[QUEUE_FILE_MAX];
    size_t len = 0;

    if (indigo_queue_encode(q, buf, sizeof buf, &len) != INDIGO_CODEC_OK) {
        return INDIGO_STORE_IO;
    }
    return indigo_file_write_atomic(path, buf, len);
}

indigo_store_status
indigo_queue_store_load(const char *path, indigo_post_queue *out)
{
    /* One byte past the largest valid file, so a longer file reads as too big. */
    static char buf[QUEUE_FILE_MAX + 1];
    size_t n = 0;
    indigo_store_status st;

    indigo_queue_init(out);
    st = indigo_file_read(path, buf, sizeof buf, &n);
    if (st != INDIGO_STORE_OK) {
        return st;
    }
    if (indigo_queue_decode(buf, n, out) == INDIGO_CODEC_OK) {
        return INDIGO_STORE_OK;
    }
    indigo_file_set_aside(path);
    return INDIGO_STORE_UNREADABLE;
}
