#include "store/drafts_store.h"

#include "store/file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DRAFTS_MAGIC "indigo-drafts 1\n"
#define DRAFTS_FIELDS 11
#define DRAFTS_FILE_MAX \
    (INDIGO_SAVED_DRAFTS_MAX * \
     (2 * (INDIGO_DRAFT_MAX + 3 * INDIGO_POST_URI_MAX + 2 * INDIGO_POST_CID_MAX + \
           INDIGO_POST_NAME_MAX + INDIGO_POST_TEXT_MAX) + 96) + 64)

void
indigo_saved_drafts_init(indigo_saved_drafts *drafts)
{
    if (drafts) {
        memset(drafts, 0, sizeof *drafts);
    }
}

bool
indigo_saved_draft_valid(const indigo_saved_draft *draft)
{
    if (!draft || draft->text[0] == '\0' ||
        strlen(draft->text) >= INDIGO_DRAFT_MAX) {
        return false;
    }
    if (draft->mode == INDIGO_COMPOSE_POST) {
        return !draft->has_target;
    }
    if (!draft->has_target || !draft->target_uri[0] || !draft->target_cid[0]) {
        return false;
    }
    if (draft->mode == INDIGO_COMPOSE_REPLY) {
        return draft->root_uri[0] && draft->root_cid[0];
    }
    return draft->mode == INDIGO_COMPOSE_QUOTE;
}

bool
indigo_saved_drafts_add(indigo_saved_drafts *drafts,
                        const indigo_saved_draft *draft)
{
    if (!drafts || !indigo_saved_draft_valid(draft) ||
        drafts->count >= INDIGO_SAVED_DRAFTS_MAX) {
        return false;
    }
    drafts->items[drafts->count] = *draft;
    drafts->count++;
    return true;
}

const indigo_saved_draft *
indigo_saved_drafts_get(const indigo_saved_drafts *drafts, unsigned index)
{
    if (!drafts || index >= drafts->count) {
        return NULL;
    }
    return &drafts->items[index];
}

bool
indigo_saved_drafts_remove(indigo_saved_drafts *drafts, unsigned index)
{
    if (!drafts || index >= drafts->count) {
        return false;
    }
    for (unsigned i = index + 1; i < drafts->count; i++) {
        drafts->items[i - 1] = drafts->items[i];
    }
    drafts->count--;
    memset(&drafts->items[drafts->count], 0, sizeof drafts->items[drafts->count]);
    return true;
}

static bool
put(char *out, size_t cap, size_t *used, const char *s, size_t n)
{
    if (*used + n + 1 > cap) {
        return false;
    }
    memcpy(out + *used, s, n);
    *used += n;
    out[*used] = '\0';
    return true;
}

static bool
put_escaped(char *out, size_t cap, size_t *used, const char *s)
{
    for (const char *p = s; *p; p++) {
        const char *rep = NULL;

        switch (*p) {
        case '\\': rep = "\\\\"; break;
        case '\n': rep = "\\n";  break;
        case '\r': rep = "\\r";  break;
        case '\t': rep = "\\t";  break;
        default:   break;
        }
        if (rep ? !put(out, cap, used, rep, 2) : !put(out, cap, used, p, 1)) {
            return false;
        }
    }
    return true;
}

static bool
put_field(char *out, size_t cap, size_t *used, const char *s)
{
    return put_escaped(out, cap, used, s) && put(out, cap, used, "\t", 1);
}

static bool
put_number(char *out, size_t cap, size_t *used, long long n)
{
    char buf[32];

    snprintf(buf, sizeof buf, "%lld\t", n);
    return put(out, cap, used, buf, strlen(buf));
}

static bool
unescape(const char *s, size_t n, char *dst, size_t cap)
{
    size_t o = 0;

    for (size_t i = 0; i < n; i++) {
        char c = s[i];

        if (c == '\\') {
            if (++i >= n) {
                return false;
            }
            switch (s[i]) {
            case '\\': c = '\\'; break;
            case 'n':  c = '\n'; break;
            case 'r':  c = '\r'; break;
            case 't':  c = '\t'; break;
            default:   return false;
            }
        }
        if (o + 1 >= cap) {
            return false;
        }
        dst[o++] = c;
    }
    dst[o] = '\0';
    return true;
}

static bool
split_fields(const char *line, size_t len, const char **field, size_t *field_len)
{
    int n = 0;
    const char *start = line;

    for (size_t i = 0; i <= len; i++) {
        if (i == len || line[i] == '\t') {
            if (n >= DRAFTS_FIELDS) {
                return false;
            }
            field[n] = start;
            field_len[n] = (size_t) (&line[i] - start);
            n++;
            start = line + i + 1;
        }
    }
    return n == DRAFTS_FIELDS;
}

static bool
parse_number(const char *s, size_t n, long long *out)
{
    char buf[32];
    char *end = NULL;

    if (n == 0 || n >= sizeof buf) {
        return false;
    }
    memcpy(buf, s, n);
    buf[n] = '\0';
    *out = strtoll(buf, &end, 10);
    return end && *end == '\0';
}

indigo_codec_status
indigo_drafts_encode(const indigo_saved_drafts *drafts, char *out, size_t cap,
                     size_t *len)
{
    size_t used = 0;

    if (!drafts || !out || drafts->count > INDIGO_SAVED_DRAFTS_MAX) {
        return INDIGO_CODEC_TOO_BIG;
    }
    if (!put(out, cap, &used, DRAFTS_MAGIC, sizeof DRAFTS_MAGIC - 1)) {
        return INDIGO_CODEC_TOO_BIG;
    }
    for (unsigned i = 0; i < drafts->count; i++) {
        const indigo_saved_draft *d = &drafts->items[i];

        if (!indigo_saved_draft_valid(d) ||
            !put_number(out, cap, &used, d->saved_at) ||
            !put_number(out, cap, &used, (long long) d->mode) ||
            !put_number(out, cap, &used, (long long) d->reply_gate) ||
            !put_number(out, cap, &used, d->has_target ? 1 : 0) ||
            !put_field(out, cap, &used, d->text) ||
            !put_field(out, cap, &used, d->target_uri) ||
            !put_field(out, cap, &used, d->target_cid) ||
            !put_field(out, cap, &used, d->target_handle) ||
            !put_field(out, cap, &used, d->target_text) ||
            !put_field(out, cap, &used, d->root_uri) ||
            !put_escaped(out, cap, &used, d->root_cid) ||
            !put(out, cap, &used, "\n", 1)) {
            return INDIGO_CODEC_TOO_BIG;
        }
    }
    if (!put(out, cap, &used, "end\n", 4)) {
        return INDIGO_CODEC_TOO_BIG;
    }
    if (len) {
        *len = used;
    }
    return INDIGO_CODEC_OK;
}

indigo_codec_status
indigo_drafts_decode(const char *data, size_t len, indigo_saved_drafts *out)
{
    const size_t magic = sizeof DRAFTS_MAGIC - 1u;
    const char *p;
    const char *end;

    indigo_saved_drafts_init(out);
    if (!data || len == 0) {
        return INDIGO_CODEC_EMPTY;
    }
    if (len < magic || memcmp(data, DRAFTS_MAGIC, magic) != 0) {
        return len >= 13 && memcmp(data, "indigo-drafts", 13) == 0
                   ? INDIGO_CODEC_BAD_VERSION
                   : INDIGO_CODEC_CORRUPT;
    }
    p = data + magic;
    end = data + len;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t) (end - p));
        const char *field[DRAFTS_FIELDS];
        size_t field_len[DRAFTS_FIELDS];
        long long stamp, mode, gate, has_target;
        indigo_saved_draft *d;

        if (!nl) {
            goto bad;
        }
        if (nl - p == 3 && memcmp(p, "end", 3) == 0) {
            return INDIGO_CODEC_OK;
        }
        if (out->count >= INDIGO_SAVED_DRAFTS_MAX ||
            !split_fields(p, (size_t) (nl - p), field, field_len) ||
            !parse_number(field[0], field_len[0], &stamp) ||
            !parse_number(field[1], field_len[1], &mode) ||
            !parse_number(field[2], field_len[2], &gate) ||
            !parse_number(field[3], field_len[3], &has_target) ||
            mode < INDIGO_COMPOSE_POST || mode > INDIGO_COMPOSE_QUOTE) {
            goto bad;
        }
        d = &out->items[out->count];
        memset(d, 0, sizeof *d);
        d->saved_at = stamp;
        d->mode = (indigo_compose_mode) mode;
        d->reply_gate = (indigo_reply_gate) gate;
        d->has_target = has_target != 0;
        if (!unescape(field[4], field_len[4], d->text, sizeof d->text) ||
            !unescape(field[5], field_len[5], d->target_uri, sizeof d->target_uri) ||
            !unescape(field[6], field_len[6], d->target_cid, sizeof d->target_cid) ||
            !unescape(field[7], field_len[7], d->target_handle,
                      sizeof d->target_handle) ||
            !unescape(field[8], field_len[8], d->target_text, sizeof d->target_text) ||
            !unescape(field[9], field_len[9], d->root_uri, sizeof d->root_uri) ||
            !unescape(field[10], field_len[10], d->root_cid, sizeof d->root_cid) ||
            !indigo_saved_draft_valid(d)) {
            goto bad;
        }
        out->count++;
        p = nl + 1;
    }

bad:
    indigo_saved_drafts_init(out);
    return INDIGO_CODEC_INCOMPLETE;
}

indigo_store_status
indigo_drafts_store_save(const char *path, const indigo_saved_drafts *drafts)
{
    static char buf[DRAFTS_FILE_MAX];
    size_t len = 0;

    if (!path || indigo_drafts_encode(drafts, buf, sizeof buf, &len) != INDIGO_CODEC_OK) {
        return INDIGO_STORE_IO;
    }
    return indigo_file_write_atomic(path, buf, len);
}

indigo_store_status
indigo_drafts_store_load(const char *path, indigo_saved_drafts *out)
{
    static char buf[DRAFTS_FILE_MAX + 1];
    size_t n = 0;
    indigo_store_status st;

    indigo_saved_drafts_init(out);
    st = indigo_file_read(path, buf, sizeof buf, &n);
    if (st != INDIGO_STORE_OK) {
        return st;
    }
    if (indigo_drafts_decode(buf, n, out) == INDIGO_CODEC_OK) {
        return INDIGO_STORE_OK;
    }
    indigo_file_set_aside(path);
    return INDIGO_STORE_UNREADABLE;
}
