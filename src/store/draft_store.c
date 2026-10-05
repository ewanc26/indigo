#include "store/draft_store.h"

#include <stdbool.h>
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
    char tmp[300];
    char *buf;
    size_t cap;
    size_t len = 0;
    FILE *f;
    bool ok;

    if (!path || !text) {
        return INDIGO_STORE_IO;
    }
    if (text[0] == '\0') {
        if (remove(path) != 0 && (f = fopen(path, "rb")) != NULL) {
            fclose(f);
            return INDIGO_STORE_IO;
        }
        return INDIGO_STORE_OK;
    }
    cap = strlen(text) + DRAFT_OVERHEAD;
    buf = malloc(cap);
    if (!buf) {
        return INDIGO_STORE_IO;
    }
    if (indigo_draft_encode(text, buf, cap, &len) != INDIGO_CODEC_OK ||
        snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int) sizeof tmp) {
        free(buf);
        return INDIGO_STORE_IO;
    }
    f = fopen(tmp, "wb");
    if (!f) {
        free(buf);
        return INDIGO_STORE_IO;
    }
    ok = fwrite(buf, 1, len, f) == len;
    ok = (fclose(f) == 0) && ok;
    free(buf);
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
indigo_draft_store_load(const char *path, char *out, size_t cap)
{
    FILE *f;
    char *buf;
    size_t n;
    bool err;
    char bad[300];

    if (!path || !out || cap == 0) {
        return INDIGO_STORE_IO;
    }
    out[0] = '\0';
    f = fopen(path, "rb");
    if (!f) {
        return INDIGO_STORE_MISSING;
    }
    buf = malloc(cap + DRAFT_OVERHEAD + 1u);
    if (!buf) {
        fclose(f);
        return INDIGO_STORE_IO;
    }
    n = fread(buf, 1, cap + DRAFT_OVERHEAD + 1u, f);
    err = ferror(f) != 0;
    fclose(f);
    if (err) {
        free(buf);
        return INDIGO_STORE_IO;
    }
    if (indigo_draft_decode(buf, n, out, cap) == INDIGO_CODEC_OK) {
        free(buf);
        return INDIGO_STORE_OK;
    }
    free(buf);
    out[0] = '\0';
    /* Kept for inspection rather than discarded: it may be someone's writing. */
    if (snprintf(bad, sizeof bad, "%s.bad", path) < (int) sizeof bad) {
        remove(bad);
        rename(path, bad);
    }
    return INDIGO_STORE_UNREADABLE;
}
