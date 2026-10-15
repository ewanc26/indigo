#ifndef INDIGO_APP_TIMELINE_CACHE_H
#define INDIGO_APP_TIMELINE_CACHE_H

/* Moves the home timeline between the app's rows and the on-card cache. Only
 * what a row needs to be drawn is kept; embeds, counts and viewer state are
 * not cached, so a restored row is a placeholder until the network answers and
 * the refresh replaces the whole list. */

#include "app/timeline.h"
#include "store/cache_codec.h"

/* Copy the rows of `t`, up to a cache page, into `out`. */
void indigo_timeline_cache_capture(const indigo_timeline *t, indigo_cache_timeline *out);

/* Put the cached rows into `t`, which should be empty. Restored rows carry no
 * cursor, so they never trigger a page fetch on their own. Returns how many
 * rows were restored. */
unsigned indigo_timeline_cache_restore(indigo_timeline *t, const indigo_cache_timeline *in);

#endif
