#ifndef INDIGO_ATPROTO_PREFS_H
#define INDIGO_ATPROTO_PREFS_H

/* The slice of the account's saved preferences Indigo honours when it draws a
 * feed: muted words and "hide reposts" on the home timeline. Kept apart from
 * the session code so it can be tested on the host. The matching rules are
 * Wolfram's (wolfram/muted_words.h), not Indigo's own: whole words for a single
 * word, substring for a phrase, tags exact, expired mutes skipped. */

#include "app/timeline.h"

#include <stdbool.h>

#define INDIGO_PREFS_WORDS_MAX 48
#define INDIGO_PREFS_WORD_MAX 64
#define INDIGO_PREFS_EXPIRES_MAX 40

typedef struct {
    char value[INDIGO_PREFS_WORD_MAX];
    bool content;            /* applies to post text */
    bool tag;                /* applies to hashtags */
    char expires_at[INDIGO_PREFS_EXPIRES_MAX]; /* lexicon datetime, or "" for none */
} indigo_muted_word;

typedef struct {
    indigo_muted_word words[INDIGO_PREFS_WORDS_MAX];
    unsigned count;
    bool hide_reposts;       /* home timeline only */
    long long now;           /* Unix seconds when the list was loaded; 0 = clock unknown,
                              * and then no mute counts as expired */
} indigo_prefs;

void indigo_prefs_clear(indigo_prefs *prefs);

/* Add a word. Empty values and a full list are ignored. A word with neither
 * target is treated as content-only, which is what the server means by the
 * default. `expires_at` is the mute's lexicon datetime, or NULL for none; it
 * is checked against `prefs->now` when matching. */
bool indigo_prefs_add_word(indigo_prefs *prefs, const char *value, bool content,
                           bool tag, const char *expires_at);

/* Whether `text` (and `tags`, which may be NULL) contains a muted word.
 * Matching is Wolfram's: case-insensitive, a single alphanumeric word matches
 * whole words only (so muting "cat" does not hide "category"), a phrase or a
 * word with punctuation in it matches as a substring. */
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
