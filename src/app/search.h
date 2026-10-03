#ifndef INDIGO_SEARCH_H
#define INDIGO_SEARCH_H

#include "app/timeline.h"

#include <stdbool.h>

/* Actor search is a prompt plus a bounded result list. The bound is
 * deliberate: searchActors is paged and an unbounded result set is exactly
 * the allocation mistake the 3DS cannot afford (see AGENTS.md §17). */
#define INDIGO_SEARCH_MAX 20
#define INDIGO_SEARCH_QUERY_MAX 64
/* Results visible at once on the bottom screen. Same three-row window as the
 * timeline: a 3DS list taller than that stops being scannable. */
#define INDIGO_SEARCH_ROWS 3
/* did:plc: identifiers are short, but a custom-domain did is whatever the
 * domain's PLC record allows, so leave room rather than truncating one. */
#define INDIGO_SEARCH_DID_MAX 128

/* One result. Wolfram's typed searchActors view carries did, handle,
 * display_name and avatar; the avatar is an image URL, and Indigo has no
 * image pipeline yet (AGENTS.md §24 phase 6), so it is not kept. Opening the
 * profile fetches the full record. */
typedef struct {
    char handle[INDIGO_POST_NAME_MAX];
    char display_name[INDIGO_POST_NAME_MAX];
    char did[INDIGO_SEARCH_DID_MAX];
} indigo_actor;

typedef struct {
    char query[INDIGO_SEARCH_QUERY_MAX];
    indigo_actor items[INDIGO_SEARCH_MAX];
    unsigned count;
    unsigned selected;
    unsigned scroll;
    bool loading;
    /* Distinguishes "no search yet" from "searched and found nobody", which
     * otherwise read the same on screen. */
    bool searched;
    char status[INDIGO_POST_NOTE_MAX];
    bool status_is_error;
} indigo_search;

void indigo_search_clear(indigo_search *s);
bool indigo_search_move(indigo_search *s, int delta, unsigned rows);
bool indigo_search_select(indigo_search *s, unsigned index, unsigned rows);
const indigo_actor *indigo_search_selected(const indigo_search *s);
/* The result shown in visible row `row`, or NULL when that row is empty. */
const indigo_actor *indigo_search_row(const indigo_search *s, unsigned row, unsigned rows);
/* A search needs something to search for and must not already be running. */
bool indigo_search_can_submit(const indigo_search *s);

#endif
