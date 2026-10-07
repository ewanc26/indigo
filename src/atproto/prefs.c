#include "atproto/prefs.h"

#include <wolfram/muted_words.h>

#include <stdio.h>
#include <string.h>

void
indigo_prefs_clear(indigo_prefs *prefs)
{
    if (prefs) {
        memset(prefs, 0, sizeof *prefs);
    }
}

bool
indigo_prefs_add_word(indigo_prefs *prefs, const char *value, bool content,
                      bool tag, const char *expires_at)
{
    if (!prefs || !value || !value[0] || prefs->count >= INDIGO_PREFS_WORDS_MAX) {
        return false;
    }
    indigo_muted_word *w = &prefs->words[prefs->count++];

    snprintf(w->value, sizeof w->value, "%s", value);
    w->content = content || !tag;
    w->tag = tag;
    snprintf(w->expires_at, sizeof w->expires_at, "%s", expires_at ? expires_at : "");
    return true;
}

bool
indigo_prefs_text_is_muted(const indigo_prefs *prefs, const char *text,
                           const char *const *tags, int tag_count)
{
    wf_actor_pref_muted_word words[INDIGO_PREFS_WORDS_MAX];
    char *targets[INDIGO_PREFS_WORDS_MAX][2];
    char content_target[] = "content";
    char tag_target[] = "tag";
    char values[INDIGO_PREFS_WORDS_MAX][INDIGO_PREFS_WORD_MAX];
    char expires[INDIGO_PREFS_WORDS_MAX][INDIGO_PREFS_EXPIRES_MAX];

    if (!prefs || prefs->count == 0) {
        return false;
    }
    memset(words, 0, sizeof words);
    for (unsigned i = 0; i < prefs->count; i++) {
        const indigo_muted_word *w = &prefs->words[i];
        /* A tag mute may be written with or without the leading #. */
        const char *v = (w->tag && !w->content && w->value[0] == '#') ? w->value + 1 : w->value;

        snprintf(values[i], sizeof values[i], "%s", v);
        snprintf(expires[i], sizeof expires[i], "%s", w->expires_at);
        words[i].value = values[i];
        words[i].expires_at = expires[i][0] ? expires[i] : NULL;
        if (w->content) {
            targets[i][words[i].target_count++] = content_target;
        }
        if (w->tag) {
            targets[i][words[i].target_count++] = tag_target;
        }
        words[i].targets = targets[i];
    }
    return wf_muted_words_match(words, prefs->count, text, tags,
                                tags && tag_count > 0 ? (size_t) tag_count : 0, false,
                                prefs->now);
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
