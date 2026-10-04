#include "atproto/prefs.h"

#include <stdio.h>
#include <string.h>

/* Bytes of a multi-byte UTF-8 sequence count as letters, so a word in a script
 * without spaces or ASCII punctuation is not split in the middle. */
static bool
is_word_byte(unsigned char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') || c >= 0x80;
}

static char
lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}

static bool
ci_equal(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        if (lower(*a) != lower(*b)) {
            return false;
        }
    }
    return *a == *b;
}

static bool
word_is_plain(const char *w)
{
    for (; *w; w++) {
        if (!is_word_byte((unsigned char) *w)) {
            return false;
        }
    }
    return true;
}

/* Case-insensitive search; whole_word requires non-word bytes (or the string
 * edge) on both sides of the match. */
static bool
contains(const char *hay, const char *needle, bool whole_word)
{
    const size_t n = strlen(needle);

    if (n == 0) {
        return false;
    }
    for (const char *p = hay; *p; p++) {
        size_t i = 0;

        while (i < n && p[i] && lower(p[i]) == lower(needle[i])) {
            i++;
        }
        if (i != n) {
            continue;
        }
        if (whole_word) {
            if (p != hay && is_word_byte((unsigned char) p[-1])) {
                continue;
            }
            if (is_word_byte((unsigned char) p[n])) {
                continue;
            }
        }
        return true;
    }
    return false;
}

void
indigo_prefs_clear(indigo_prefs *prefs)
{
    if (prefs) {
        memset(prefs, 0, sizeof *prefs);
    }
}

bool
indigo_prefs_add_word(indigo_prefs *prefs, const char *value, bool content,
                      bool tag)
{
    if (!prefs || !value || !value[0] || prefs->count >= INDIGO_PREFS_WORDS_MAX) {
        return false;
    }
    indigo_muted_word *w = &prefs->words[prefs->count++];

    snprintf(w->value, sizeof w->value, "%s", value);
    w->content = content || !tag;
    w->tag = tag;
    return true;
}

bool
indigo_prefs_text_is_muted(const indigo_prefs *prefs, const char *text,
                           const char *const *tags, int tag_count)
{
    if (!prefs) {
        return false;
    }
    for (unsigned i = 0; i < prefs->count; i++) {
        const indigo_muted_word *w = &prefs->words[i];

        if (w->content && text &&
            contains(text, w->value, word_is_plain(w->value))) {
            return true;
        }
        if (w->tag && tags) {
            /* A tag mute may be written with or without the leading #. */
            const char *v = w->value[0] == '#' ? w->value + 1 : w->value;

            for (int t = 0; t < tag_count; t++) {
                if (tags[t] && ci_equal(tags[t], v)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool
indigo_prefs_post_is_hidden(const indigo_prefs *prefs, const indigo_post *post,
                            bool home)
{
    if (!prefs || !post) {
        return false;
    }
    if (home && prefs->hide_reposts && post->reposted_by[0]) {
        return true;
    }
    if (prefs->count == 0) {
        return false;
    }
    const char *tags[INDIGO_POST_FACETS_MAX];
    int tag_count = 0;

    for (unsigned i = 0; i < post->facet_count && i < INDIGO_POST_FACETS_MAX; i++) {
        if (post->facets[i].kind == INDIGO_FACET_TAG) {
            tags[tag_count++] = post->facets[i].target;
        }
    }
    return indigo_prefs_text_is_muted(prefs, post->text, tags, tag_count);
}

unsigned
indigo_prefs_filter_page(const indigo_prefs *prefs, indigo_post *page,
                          unsigned count, unsigned from, bool home)
{
    if (!prefs || !page || from > count) {
        return 0;
    }
    unsigned out = from;

    for (unsigned i = from; i < count; i++) {
        if (indigo_prefs_post_is_hidden(prefs, &page[i], home)) {
            continue;
        }
        if (out != i) {
            page[out] = page[i];
        }
        out++;
    }
    return count - out;
}
