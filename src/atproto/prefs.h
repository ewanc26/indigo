#ifndef INDIGO_ATPROTO_PREFS_H
#define INDIGO_ATPROTO_PREFS_H

/* The slice of the account's saved preferences Indigo honours when it draws a
 * feed: muted words and "hide reposts" on the home timeline. The muted-word list
 * and its matching rules are Wolfram's (wolfram/muted_words.h); what is Indigo's
 * is which posts a mute applies to and the page filter. */

#include "app/timeline.h"

#include <stdbool.h>
#include <wolfram/muted_words.h>

typedef struct {
    wf_muted_list muted;
    bool hide_reposts; /* home timeline only */
} indigo_prefs;

void indigo_prefs_clear(indigo_prefs *prefs);

/* Replace `prefs` with what the server returned; `now` (Unix seconds, 0 if the
 * clock is unusable) decides which mutes have expired. */
void indigo_prefs_from_wolfram(indigo_prefs *prefs, const wf_actor_preferences *src,
                               long long now);

/* Whether `text` (and `tags`, which may be NULL) contains a muted word. */
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

#endif
