#ifndef INDIGO_ATPROTO_PREFS_H
#define INDIGO_ATPROTO_PREFS_H

/* The slice of the account's saved preferences Indigo honours when it draws a
 * feed: muted words and "hide reposts" on the home timeline. Kept apart from
 * the session code so the matching rules can be tested on the host.
 *
 * Cobalt has the same module, written first; the rules are shared on purpose,
 * so the two clients do not diverge on what a person asked to stop seeing. */

#include "app/timeline.h"

#include <stdbool.h>

#define INDIGO_PREFS_WORDS_MAX 48
#define INDIGO_PREFS_WORD_MAX 64

typedef struct {
    char value[INDIGO_PREFS_WORD_MAX];
    bool content;            /* applies to post text */
    bool tag;                /* applies to hashtags */
} indigo_muted_word;

typedef struct {
    indigo_muted_word words[INDIGO_PREFS_WORDS_MAX];
    unsigned count;
    bool hide_reposts;       /* home timeline only */
} indigo_prefs;

void indigo_prefs_clear(indigo_prefs *prefs);

/* Add a word. Empty values and a full list are ignored. A word with neither
 * target is treated as content-only, which is what the server means by the
 * default. */
bool indigo_prefs_add_word(indigo_prefs *prefs, const char *value, bool content,
                           bool tag);

/* Whether `text` (and `tags`, which may be NULL) contains a muted word.
 * Matching is case-insensitive. A single alphanumeric word matches whole
 * words only, so muting "cat" does not hide "category"; a phrase, or a word
 * with punctuation in it, matches as a substring. */
bool indigo_prefs_text_is_muted(const indigo_prefs *prefs, const char *text,
                                const char *const *tags, int tag_count);

/* Whether one post is hidden by the preferences. `home` is true for the home
 * timeline, which is the only feed hide_reposts applies to. */
bool indigo_prefs_post_is_hidden(const indigo_prefs *prefs,
                                 const indigo_post *post, bool home);

/* Drop posts from `page` starting at index `from` that the preferences hide.
 * Returns how many were removed. The page array is the session worker's, so
 * this runs on the worker before the page is published. */
unsigned indigo_prefs_filter_page(const indigo_prefs *prefs, indigo_post *page,
                                  unsigned count, unsigned from, bool home);

#ifdef __3DS__
/* Replace `prefs` with what the server returned. Expired mutes are skipped.
 * Declared here rather than the Wolfram type included, so this header stays
 * host-portable; prefs_wolfram.c includes the real one. */
struct wf_actor_preferences;
void indigo_prefs_from_wolfram(indigo_prefs *prefs,
                               const struct wf_actor_preferences *src,
                               long long now);
#endif

#endif
