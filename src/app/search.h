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

/* One entry in an account's curated lists. A list is not a person: it has no
 * handle and no did, only a name and a URI to open. */
typedef struct {
    char name[INDIGO_POST_NAME_MAX];
    char description[INDIGO_POST_NOTE_MAX];
    char uri[INDIGO_POST_URI_MAX];
} indigo_list;

/* One screen serves seven lists. The people lists share the indigo_actor row,
 * the post lists the indigo_post row, and curated lists their own. */
typedef enum {
    INDIGO_SEARCH_PEOPLE = 0,
    INDIGO_SEARCH_FOLLOWERS,
    INDIGO_SEARCH_FOLLOWING,
    INDIGO_SEARCH_POSTS,
    INDIGO_SEARCH_AUTHOR,
    INDIGO_SEARCH_LISTS,
    INDIGO_SEARCH_LIST_MEMBERS,
} indigo_search_kind;

typedef struct {
    /* Only meaningful for INDIGO_SEARCH_PEOPLE. The other two are lists
     * around one person, so they carry the subject instead of a query. */
    char query[INDIGO_SEARCH_QUERY_MAX];
    indigo_search_kind kind;
    char subject[INDIGO_POST_NAME_MAX];
    /* Only one kind is ever on screen, and a post is roughly twenty times an
     * actor (2KB of text and URIs against 300 bytes of names), so the results
     * share storage rather than costing both. */
    union {
        indigo_actor actors[INDIGO_SEARCH_MAX];
        indigo_post posts[INDIGO_SEARCH_MAX];
        indigo_list lists[INDIGO_SEARCH_MAX];
    } results;
    /* Backing store for the curated lists, kept separately from the union:
     * a list's members are actors and land in the same union, so without
     * this the members would overwrite the lists that were just browsed.
     * 20 lists * 320 bytes is 6.4KB, the same as the posts already cost. */
    indigo_list held_lists[INDIGO_SEARCH_MAX];
    unsigned held_count;
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
/* The same two for the post list. Reading the actor view while the kind is
 * posts would reinterpret a post's first bytes as a name, so callers pick
 * with indigo_search_is_posts(). */
const indigo_post *indigo_search_selected_post(const indigo_search *s);
const indigo_post *indigo_search_row_post(const indigo_search *s, unsigned row, unsigned rows);
/* The same two for the curated lists. A list is neither a person nor a post,
 * so reading one through the other accessors would reinterpret its name as a
 * handle or a URI as text; callers pick with indigo_search_is_lists(). */
const indigo_list *indigo_search_selected_list(const indigo_search *s);
const indigo_list *indigo_search_row_list(const indigo_search *s, unsigned row, unsigned rows);
/* A search needs something to search for and must not already be running. */
bool indigo_search_can_submit(const indigo_search *s);
/* The screen title for this list, and whether the header box takes typing. */
const char *indigo_search_title(const indigo_search *s);
bool indigo_search_is_typed(const indigo_search *s);
bool indigo_search_is_posts(const indigo_search *s);
bool indigo_search_is_lists(const indigo_search *s);

#endif
