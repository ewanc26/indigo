#include "store/draft_store.h"

#include "store/file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DRAFT_MAGIC "indigo-draft 1\n"
#define DRAFT_OVERHEAD 64u

indigo_codec_status
indigo_draft_encode(const char *text, char *buf, size_t cap, size_t *len)
{
    size_t n;
    int h;

    if (!text || !buf || !len) {
        return INDIGO_CODEC_CORRUPT;
    }
    n = strlen(text);
    h = snprintf(buf, cap, DRAFT_MAGIC "len=%zu\n", n);
    if (h < 0 || (size_t) h + n + 5u >= cap) {
        return INDIGO_CODEC_TOO_BIG;
    }
    memcpy(buf + h, text, n);
    memcpy(buf + h + n, "\nend\n", 5u);
    *len = (size_t) h + n + 5u;
    return INDIGO_CODEC_OK;
}

indigo_codec_status
indigo_draft_decode(const char *buf, size_t len, char *out, size_t cap)
{
    const size_t magic = sizeof DRAFT_MAGIC - 1u;
    size_t n = 0;
    size_t i;

    if (len == 0) {
        return INDIGO_CODEC_EMPTY;
    }
    if (len < magic || memcmp(buf, DRAFT_MAGIC, magic) != 0) {
        return len >= 12 && memcmp(buf, "indigo-draft", 12) == 0
                   ? INDIGO_CODEC_BAD_VERSION
                   : INDIGO_CODEC_CORRUPT;
    }
    i = magic;
    if (len < i + 4u || memcmp(buf + i, "len=", 4) != 0) {
        return INDIGO_CODEC_CORRUPT;
    }
    for (i += 4u; i < len && buf[i] >= '0' && buf[i] <= '9'; i++) {
        n = n * 10u + (size_t) (buf[i] - '0');
        if (n >= cap) {
            return INDIGO_CODEC_TOO_BIG;
        }
    }
    if (i >= len || buf[i] != '\n') {
        return INDIGO_CODEC_CORRUPT;
    }
    i++;
    if (len - i < n + 5u) {
        return INDIGO_CODEC_INCOMPLETE;
    }
    if (memcmp(buf + i + n, "\nend\n", 5u) != 0 || len - i != n + 5u) {
        return INDIGO_CODEC_CORRUPT;
    }
    if (memchr(buf + i, '\0', n)) {
        return INDIGO_CODEC_CORRUPT;
    }
    memcpy(out, buf + i, n);
    out[n] = '\0';
    return INDIGO_CODEC_OK;
}

indigo_store_status
indigo_draft_store_save(const char *path, const char *text)
{
    char *buf;
    size_t cap;
    size_t len = 0;
    indigo_store_status st = INDIGO_STORE_IO;

    if (!path || !text) {
        return INDIGO_STORE_IO;
    }
    if (text[0] == '\0') {
        /* An empty draft is no draft: nothing to keep. */
        return indigo_file_remove(path);
    }
    cap = strlen(text) + DRAFT_OVERHEAD;
    buf = malloc(cap);
    if (!buf) {
        return INDIGO_STORE_IO;
    }
    if (indigo_draft_encode(text, buf, cap, &len) == INDIGO_CODEC_OK) {
        st = indigo_file_write_atomic(path, buf, len);
    }
    free(buf);
    return st;
}

indigo_store_status
indigo_draft_store_load(const char *path, char *out, size_t cap)
{
    char *buf;
    size_t n = 0;
    indigo_store_status st;
    indigo_codec_status decoded;

    if (!path || !out || cap == 0) {
        return INDIGO_STORE_IO;
    }
    out[0] = '\0';
    buf = malloc(cap + DRAFT_OVERHEAD + 1u);
    if (!buf) {
        return INDIGO_STORE_IO;
    }
    st = indigo_file_read(path, buf, cap + DRAFT_OVERHEAD + 1u, &n);
    if (st != INDIGO_STORE_OK) {
        free(buf);
        return st;
    }
    decoded = indigo_draft_decode(buf, n, out, cap);
    free(buf);
    if (decoded == INDIGO_CODEC_OK) {
        return INDIGO_STORE_OK;
    }
    out[0] = '\0';
    /* Kept for inspection rather than discarded: it may be someone's writing. */
    indigo_file_set_aside(path);
    return INDIGO_STORE_UNREADABLE;
}
