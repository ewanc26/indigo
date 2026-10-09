#include "app/timeline.h"

#include <string.h>

void
indigo_copy_utf8(char *dst, size_t cap, const char *src)
{
    size_t n;

    if (cap == 0) {
        return;
    }
    n = src ? strlen(src) : 0;
    if (n >= cap) {
        n = cap - 1;
        /* Never leave half of a multi-byte character behind. */
        while (n > 0 && ((unsigned char) src[n] & 0xC0) == 0x80) {
            n--;
        }
    }
    if (n > 0) {
        memcpy(dst, src, n);
    }
    dst[n] = '\0';
}

void
indigo_timeline_init(indigo_timeline *t)
{
    memset(t, 0, sizeof *t);
}

void
indigo_timeline_clear(indigo_timeline *t)
{
    t->count = 0;
    t->selected = 0;
    t->scroll = 0;
    t->cursor[0] = '\0';
    t->has_more = false;
    t->loading = false;
    t->status[0] = '\0';
    t->status_is_error = false;
}

bool
indigo_timeline_append(indigo_timeline *t, const indigo_post *p)
{
    if (t->count >= INDIGO_TIMELINE_MAX) {
        return false;
    }
    t->posts[t->count++] = *p;
    return true;
}

bool
indigo_timeline_remove_post(indigo_timeline *t, const char *uri)
{
    unsigned out = 0;
    bool removed = false;
    unsigned old_selected = t->selected;

    if (!t || !uri || !uri[0]) {
        return false;
    }
    for (unsigned i = 0; i < t->count; i++) {
        if (strcmp(t->posts[i].uri, uri) == 0) {
            removed = true;
            continue;
        }
        if (out != i) {
            t->posts[out] = t->posts[i];
        }
        out++;
    }
    if (!removed) {
        return false;
    }
    t->count = out;
    if (out == 0) {
        t->selected = 0;
        t->scroll = 0;
    } else {
        if (old_selected > t->count) {
            old_selected = t->count;
        }
        t->selected = old_selected;
        if (t->selected >= t->count) {
            t->selected = t->count - 1;
        }
        keep_visible(t, INDIGO_TIMELINE_ROWS);
    }
    return true;
}

static void
keep_visible(indigo_timeline *t, unsigned rows)
{
    if (rows == 0) {
        return;
    }
    if (t->selected < t->scroll) {
        t->scroll = t->selected;
    } else if (t->selected >= t->scroll + rows) {
        t->scroll = t->selected - rows + 1;
    }
    if (t->count > rows && t->scroll > t->count - rows) {
        t->scroll = t->count - rows;
    }
    if (t->count <= rows) {
        t->scroll = 0;
    }
}

bool
indigo_timeline_select(indigo_timeline *t, unsigned index, unsigned rows)
{
    unsigned before = t->selected;

    if (t->count == 0) {
        t->selected = 0;
        t->scroll = 0;
        return false;
    }
    t->selected = index >= t->count ? t->count - 1 : index;
    keep_visible(t, rows);
    return t->selected != before;
}

bool
indigo_timeline_move(indigo_timeline *t, int delta, unsigned rows)
{
    long target = (long) t->selected + delta;

    if (target < 0) {
        target = 0;
    }
    return indigo_timeline_select(t, (unsigned) target, rows);
}

const indigo_post *
indigo_timeline_selected(const indigo_timeline *t)
{
    return t->count ? &t->posts[t->selected] : NULL;
}

bool
indigo_timeline_wants_page(const indigo_timeline *t)
{
    if (t->loading || !t->has_more || t->cursor[0] == '\0') {
        return false;
    }
    if (t->count >= INDIGO_TIMELINE_MAX) {
        return false;
    }
    return t->selected + INDIGO_TIMELINE_PREFETCH >= t->count;
}

void
indigo_timeline_begin_fetch(indigo_timeline *t, bool reset)
{
    if (reset) {
        indigo_timeline_clear(t);
    }
    t->loading = true;
    t->status[0] = '\0';
    t->status_is_error = false;
}

void
indigo_timeline_finish_fetch(indigo_timeline *t, const char *next_cursor)
{
    t->loading = false;
    indigo_copy_utf8(t->cursor, sizeof t->cursor, next_cursor);
    t->has_more = t->cursor[0] != '\0' && t->count < INDIGO_TIMELINE_MAX;
    if (t->count == 0) {
        indigo_copy_utf8(t->status, sizeof t->status, "Nothing here yet.");
    }
}

void
indigo_timeline_fail_fetch(indigo_timeline *t, const char *message)
{
    t->loading = false;
    indigo_copy_utf8(t->status, sizeof t->status, message);
    t->status_is_error = true;
}

static indigo_post *
find(indigo_timeline *t, const char *uri)
{
    for (unsigned i = 0; i < t->count; i++) {
        if (strcmp(t->posts[i].uri, uri) == 0) {
            return &t->posts[i];
        }
    }
    return NULL;
}

bool
indigo_timeline_set_like(indigo_timeline *t, const char *post_uri,
                         const char *like_uri, bool pending)
{
    indigo_post *p = find(t, post_uri);

    if (!p) {
        return false;
    }
    p->like_pending = pending;
    if (!pending) {
        bool had = p->like_uri[0] != '\0';
        indigo_copy_utf8(p->like_uri, sizeof p->like_uri, like_uri);
        if (!had && p->like_uri[0]) {
            p->like_count++;
        } else if (had && !p->like_uri[0] && p->like_count > 0) {
            p->like_count--;
        }
    }
    return true;
}

bool
indigo_timeline_set_repost(indigo_timeline *t, const char *post_uri,
                           const char *repost_uri, bool pending)
{
    indigo_post *p = find(t, post_uri);

    if (!p) {
        return false;
    }
    p->repost_pending = pending;
    if (!pending) {
        bool had = p->repost_uri[0] != '\0';
        indigo_copy_utf8(p->repost_uri, sizeof p->repost_uri, repost_uri);
        if (!had && p->repost_uri[0]) {
            p->repost_count++;
        } else if (had && !p->repost_uri[0] && p->repost_count > 0) {
            p->repost_count--;
        }
    }
    return true;
}
