#ifndef INDIGO_SEARCH_H
#define INDIGO_SEARCH_H

#include "app/timeline.h"
#include "atproto/feeds.h"
#include "atproto/starter_pack.h"

#include <stdbool.h>

/* The session fetches a page of results at a time, the same size as every
 * other paged list the worker returns. */
#define INDIGO_SEARCH_PAGE 20
/* Results the app holds across pages. Bounded on purpose: an unbounded
 * result set is exactly the allocation mistake the 3DS cannot afford (see
 * AGENTS.md §17). The timeline holds the same number. */
#define INDIGO_SEARCH_MAX 60
#define INDIGO_SEARCH_QUERY_MAX 64
/* Results visible at once on the bottom screen. Same three-row window as the
 * timeline: a 3DS list taller than that stops being scannable. */
#define INDIGO_SEARCH_ROWS 3
/* did:plc: identifiers are short, but a custom-domain did is whatever the
 * domain's PLC record allows, so leave room rather than truncating one. */
#define INDIGO_SEARCH_DID_MAX 128
#define INDIGO_LIST_PURPOSE_MAX 64
#define INDIGO_LIST_SUBJECT_MAX 256

#define INDIGO_LIST_PURPOSE_CURATE "app.bsky.graph.defs#curatelist"
#define INDIGO_LIST_PURPOSE_MODERATE "app.bsky.graph.defs#modlist"

/* One result. Wolfram's typed searchActors view carries did, handle,
 * display_name and avatar; opening the profile fetches the full record. */
typedef struct {
    char handle[INDIGO_POST_NAME_MAX];
    char display_name[INDIGO_POST_NAME_MAX];
    char did[INDIGO_SEARCH_DID_MAX];
    char avatar[INDIGO_MEDIA_URL_MAX];
} indigo_actor;

/* One entry in an account's curated lists. A list is not a person: it has no
 * handle and no did, only a name and a URI to open. */
typedef struct {
    char name[INDIGO_POST_NAME_MAX];
    char description[INDIGO_POST_NOTE_MAX];
    char uri[INDIGO_POST_URI_MAX];
    char purpose[INDIGO_LIST_PURPOSE_MAX];
} indigo_list;

/* One screen serves ten lists. The people lists share the indigo_actor row,
 * the post lists the indigo_post row, and curated lists their own. */
typedef enum {
    INDIGO_SEARCH_PEOPLE = 0,
    INDIGO_SEARCH_FOLLOWERS,
    INDIGO_SEARCH_FOLLOWING,
    INDIGO_SEARCH_POSTS,
    INDIGO_SEARCH_AUTHOR,
    INDIGO_SEARCH_LISTS,
    INDIGO_SEARCH_LIST_MEMBERS,
    INDIGO_SEARCH_FEEDS,
    INDIGO_SEARCH_MUTED,
    INDIGO_SEARCH_BLOCKED,
    /* The people who liked or reposted one post; the subject is its URI. */
    INDIGO_SEARCH_LIKED_BY,
    INDIGO_SEARCH_REPOSTED_BY,
    /* One starter pack: its members are the search screen's actor rows, and
     * the pack's feed references follow them as list-shaped rows. */
    INDIGO_SEARCH_STARTER_PACK,
    /* Suggested feed generators to browse, and one generator's detail with
     * the save/pin/remove actions. */
    INDIGO_SEARCH_FEED_DISCOVERY,
    INDIGO_SEARCH_FEED_DETAIL,
} indigo_search_kind;

typedef struct {
    /* Only meaningful for INDIGO_SEARCH_PEOPLE. The other two are lists
     * around one person, so they carry the subject instead of a query. */
    char query[INDIGO_SEARCH_QUERY_MAX];
    indigo_search_kind kind;
    /* The profile tab (wf_profile_tab) an INDIGO_SEARCH_AUTHOR list shows. */
    int tab;
    /* A handle for the people lists, a post's at:// URI for liked-by and
     * reposted-by, so it is sized for the longer of the two. */
    char subject[INDIGO_POST_URI_MAX];
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
    /* Meaningful only for INDIGO_SEARCH_STARTER_PACK. Held here rather than in
     * a screen of its own so paging, errors and Back use the same machinery. */
    indigo_starter_pack pack;
    /* The list whose members are open, kept apart from `subject` (the name)
     * so adding and removing know which record to change. */
    char list_uri[INDIGO_POST_URI_MAX];
    char list_name[INDIGO_POST_NAME_MAX];
    /* The inline list editor's working copy. */
    bool list_editing;
    bool list_creating;
    bool list_saving;
    int list_field;
    char edit_name[INDIGO_POST_NAME_MAX];
    char edit_description[INDIGO_POST_NOTE_MAX];
    char edit_purpose[INDIGO_LIST_PURPOSE_MAX];
    bool list_adding;
    char add_subject[INDIGO_LIST_SUBJECT_MAX];
    int list_remove_confirm;
    char list_status[INDIGO_POST_NOTE_MAX];
    bool list_status_error;
    /* Discovered generators for INDIGO_SEARCH_FEED_DISCOVERY, held apart
     * from the union because a generator is neither a person, a post nor a
     * list. */
    indigo_feed_generator generators[INDIGO_FEED_DISCOVERY_PAGE];
    /* The generator INDIGO_SEARCH_FEED_DETAIL is showing. */
    indigo_feed_generator open_generator;
    bool has_open_generator;
    /* Where B from the feed detail goes: the discovery list or the picker. */
    indigo_search_kind feed_detail_back;
    /* Removing a feed takes two presses of the remove button. */
    int feed_remove_confirm;
    unsigned count;
    unsigned selected;
    unsigned scroll;
    bool loading;
    /* The cursor for the next page, empty when there is no more. */
    char cursor[INDIGO_CURSOR_MAX];
    bool has_more;
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
/* Members plus included feeds for a starter pack, or just the rows for every
 * other kind. */
unsigned indigo_search_row_count(const indigo_search *s);
/* The feed row at/after the member rows, or empty when the selection is an
 * account or the kind is not a starter pack. */
const indigo_feed_generator *indigo_search_selected_generator(const indigo_search *s);
const indigo_feed_generator *indigo_search_row_generator(const indigo_search *s,
                                                         unsigned row, unsigned rows);
const char *indigo_search_selected_feed(const indigo_search *s);
const char *indigo_search_row_feed(const indigo_search *s, unsigned row,
                                   unsigned rows);
/* A search needs something to search for and must not already be running. */
bool indigo_search_can_submit(const indigo_search *s);
/* A next page is worth starting: not already running, a cursor in hand, and
 * the person near the end of what is held. */
bool indigo_search_wants_page(const indigo_search *s);
/* Start/finish a page fetch. The fetch replaces the list; a page appends. */
void indigo_search_begin_page(indigo_search *s, bool append);
void indigo_search_finish_page(indigo_search *s, unsigned added, const char *next_cursor);
void indigo_search_fail_page(indigo_search *s, const char *message);
/* The screen title for this list, and whether the header box takes typing. */
const char *indigo_search_title(const indigo_search *s);
/* Purpose label/cycle and editor validation; pure and host-tested. */
const char *indigo_list_purpose_label(const char *purpose);
const char *indigo_list_purpose_next(const char *purpose);
bool indigo_search_list_edit_valid(const char *name, const char *description,
                                   const char *purpose);
bool indigo_search_is_typed(const indigo_search *s);
bool indigo_search_is_posts(const indigo_search *s);
bool indigo_search_is_lists(const indigo_search *s);

#endif
