#ifndef INDIGO_TIMELINE_H
#define INDIGO_TIMELINE_H

#include <stdbool.h>
#include <stddef.h>

/* Bounded, allocation-free storage: the 3DS has little RAM. */
#define INDIGO_TIMELINE_MAX 60
#define INDIGO_POST_TEXT_MAX 512
#define INDIGO_POST_FACETS_MAX 8
#define INDIGO_POST_URI_MAX 160
#define INDIGO_POST_CID_MAX 72
#define INDIGO_POST_NAME_MAX 64
#define INDIGO_POST_NOTE_MAX 96
/* Facet target: a did for a mention, a URI for a link, a bare tag. Long
 * targets are truncated; they are only ever shown, never fetched. */
#define INDIGO_FACET_TARGET_MAX 96
#define INDIGO_CURSOR_MAX 128
/* Ask for the next page when this few posts remain below the selection. */
#define INDIGO_TIMELINE_PREFETCH 5

typedef enum {
    INDIGO_FACET_LINK = 0,
    INDIGO_FACET_MENTION,
    INDIGO_FACET_TAG,
} indigo_facet_kind;

typedef struct {
    indigo_facet_kind kind;
    /* Byte range within text. */
    unsigned start;
    unsigned end;
    /* What the facet points at, from Wolfram: a did, a link URI or a tag. */
    char target[INDIGO_FACET_TARGET_MAX];
} indigo_post_facet;

typedef struct {
    char uri[INDIGO_POST_URI_MAX];
    char cid[INDIGO_POST_CID_MAX];
    char handle[INDIGO_POST_NAME_MAX];
    char display_name[INDIGO_POST_NAME_MAX];
    char text[INDIGO_POST_TEXT_MAX];
    indigo_post_facet facets[INDIGO_POST_FACETS_MAX];
    unsigned facet_count;
    /* "Reposted by X" or empty. */
    char reposted_by[INDIGO_POST_NAME_MAX];
    /* A one-line summary of an image, link or quote embed, or empty. */
    char embed_note[INDIGO_POST_NOTE_MAX];
    bool is_reply;
    /* Nesting in a thread view: 0 for ancestors, the post itself and the
     * timeline; replies are deeper. */
    unsigned char depth;
    unsigned like_count;
    unsigned repost_count;
    unsigned reply_count;
    /* Record URIs of the viewer's own like/repost, empty when none. */
    char like_uri[INDIGO_POST_URI_MAX];
    char repost_uri[INDIGO_POST_URI_MAX];
    /* An action is in flight for this post. */
    bool like_pending;
    bool repost_pending;
} indigo_post;

typedef struct {
    indigo_post posts[INDIGO_TIMELINE_MAX];
    unsigned count;
    unsigned selected;
    unsigned scroll;
    char cursor[INDIGO_CURSOR_MAX];
    bool has_more;
    bool loading;
    /* A line for the person, never containing credentials. */
    char status[INDIGO_POST_NOTE_MAX];
    bool status_is_error;
} indigo_timeline;

void indigo_timeline_init(indigo_timeline *t);
void indigo_timeline_clear(indigo_timeline *t);

/* Copy a post in; false (and nothing stored) when the list is full. */
bool indigo_timeline_append(indigo_timeline *t, const indigo_post *p);

/* Move the selection, clamped to the list; keeps it inside the window of
 * `rows` visible rows. Returns true if the selection changed. */
bool indigo_timeline_move(indigo_timeline *t, int delta, unsigned rows);
bool indigo_timeline_select(indigo_timeline *t, unsigned index, unsigned rows);
const indigo_post *indigo_timeline_selected(const indigo_timeline *t);

/* True when the person is close to the end and more can be fetched. */
bool indigo_timeline_wants_page(const indigo_timeline *t);

/* Start/finish a fetch. `reset` replaces the list; otherwise it appends. */
void indigo_timeline_begin_fetch(indigo_timeline *t, bool reset);
void indigo_timeline_finish_fetch(indigo_timeline *t, const char *next_cursor);
void indigo_timeline_fail_fetch(indigo_timeline *t, const char *message);

/* Like/repost state, keyed by post URI (the list may have been replaced while
 * the call was in flight). Pending only marks the post busy; the count and
 * the viewer URI change when the call finishes (pending=false). A non-empty
 * URI means liked/reposted, empty means undone. False if the post is gone. */
bool indigo_timeline_set_like(indigo_timeline *t, const char *post_uri,
                              const char *like_uri, bool pending);
bool indigo_timeline_set_repost(indigo_timeline *t, const char *post_uri,
                                const char *repost_uri, bool pending);

/* Copy `src` into `dst` of `cap` bytes, truncating on a UTF-8 boundary. */
void indigo_copy_utf8(char *dst, size_t cap, const char *src);

#endif
