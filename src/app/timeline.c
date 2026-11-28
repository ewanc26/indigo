#include "app/timeline.h"

#include <stdio.h>
#include <string.h>

static void keep_visible(indigo_timeline *t, unsigned rows);

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
indigo_timeline_remove_post(indigo_timeline *t, const char *uri, unsigned rows)
{
    unsigned out = 0;
    unsigned removed_before_selected = 0;
    bool removed = false;
    unsigned old_selected;

    if (!t || !uri || !uri[0]) {
        return false;
    }
    old_selected = t->selected;
    for (unsigned i = 0; i < t->count; i++) {
        if (strcmp(t->posts[i].uri, uri) == 0) {
            removed = true;
            if (i < old_selected) {
                removed_before_selected++;
            }
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
        old_selected = old_selected > removed_before_selected
                           ? old_selected - removed_before_selected
                           : 0;
        t->selected = old_selected < t->count ? old_selected : t->count - 1;
        keep_visible(t, rows);
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
    t->awaiting_first_page = reset;
    t->status[0] = '\0';
    t->status_is_error = false;
}

bool
indigo_timeline_finish_fetch(indigo_timeline *t, const char *next_cursor)
{
    bool first = t->awaiting_first_page;

    t->loading = false;
    t->awaiting_first_page = false;
    indigo_copy_utf8(t->cursor, sizeof t->cursor, next_cursor);
    t->has_more = t->cursor[0] != '\0' && t->count < INDIGO_TIMELINE_MAX;
    if (t->count == 0) {
        indigo_copy_utf8(t->status, sizeof t->status, "Nothing here yet.");
    }
    return first;
}

void
indigo_timeline_fail_fetch(indigo_timeline *t, const char *message)
{
    t->loading = false;
    t->awaiting_first_page = false;
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
indigo_timeline_set_bookmarked(indigo_timeline *t, const char *post_uri,
                               bool bookmarked)
{
    indigo_post *p = find(t, post_uri);

    if (!p) {
        return false;
    }
    p->bookmarked = bookmarked;
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

/* --- content-label covers --- */

static char s_revealed[INDIGO_POST_URI_MAX];

void
indigo_post_reveal(const char *uri)
{
    indigo_copy_utf8(s_revealed, sizeof s_revealed, uri ? uri : "");
}

/* A filtered post is one the account asked never to see in a list; where a
 * list shows it anyway its cover stays, as a no_override one does. */
static bool
can_lift(const indigo_post *p)
{
    return !p->mod.no_override && p->mod.action != INDIGO_MOD_FILTER;
}

bool
indigo_post_covered(const indigo_post *p)
{
    if (!p || p->mod.action == INDIGO_MOD_NONE) {
        return false;
    }
    if (!can_lift(p)) {
        return true;
    }
    return !(s_revealed[0] && strcmp(s_revealed, p->uri) == 0);
}

bool
indigo_post_can_reveal(const indigo_post *p)
{
    return indigo_post_covered(p) && can_lift(p);
}

static void
clear_embed(indigo_post *out)
{
    out->embed_kind = INDIGO_EMBED_NONE;
    out->embed_w = 0;
    out->embed_h = 0;
    out->embed_count = 0;
    out->embed_thumb[0] = '\0';
    out->embed_alt[0] = '\0';
    out->embed_title[0] = '\0';
    out->embed_uri[0] = '\0';
    out->embed_playlist[0] = '\0';
    out->embed_duration[0] = '\0';
}

/* Append `src` to the string in `dst` (capacity `cap`), truncating at a UTF-8
 * boundary. Used instead of a formatted append: the compiler cannot prove a
 * formatted write into a tail of a buffer fits, and warns. */
static void
append_text(char *dst, size_t cap, const char *src)
{
    const size_t n = strlen(dst);

    if (n < cap) {
        indigo_copy_utf8(dst + n, cap - n, src);
    }
}

void
indigo_post_cover(const indigo_post *p, indigo_post *out)
{
    memcpy(out, p, sizeof *out);
    if (p->mod.unsupported[0]) {
        /* Named rather than dropped: Indigo was told something about this post
         * it cannot act on, which the reader should know. */
        if (out->text[0]) {
            append_text(out->text, sizeof out->text, "\n\n");
        }
        append_text(out->text, sizeof out->text, "Labelled ");
        append_text(out->text, sizeof out->text, p->mod.unsupported);
        append_text(out->text, sizeof out->text, ", which Indigo does not support yet.");
    }
    if (!indigo_post_covered(p)) {
        return;
    }
    {
        const char *reason = p->mod.reason[0] ? p->mod.reason : "Labelled content";
        const char *how = can_lift(p) ? "A: show it." : "Indigo will not show it.";

        clear_embed(out);
        if (p->mod.action == INDIGO_MOD_BLUR_MEDIA) {
            snprintf(out->embed_note, sizeof out->embed_note, "[media hidden]");
            if (out->text[0]) {
                append_text(out->text, sizeof out->text, "\n\n");
            }
            append_text(out->text, sizeof out->text, reason);
            append_text(out->text, sizeof out->text, ": media hidden. ");
            append_text(out->text, sizeof out->text, how);
            return;
        }
        out->facet_count = 0;
        out->embed_note[0] = '\0';
        snprintf(out->text, sizeof out->text, "%s. %s", reason, how);
    }
}

const indigo_post *
indigo_post_shown(const indigo_post *p)
{
    static indigo_post scratch;

    if (!p || (!indigo_post_covered(p) && !p->mod.unsupported[0])) {
        return p;
    }
    indigo_post_cover(p, &scratch);
    return &scratch;
}
