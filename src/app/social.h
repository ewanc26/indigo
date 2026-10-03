#ifndef INDIGO_SOCIAL_H
#define INDIGO_SOCIAL_H

#include "app/timeline.h"

#include <stdbool.h>

#define INDIGO_NOTIFICATION_MAX 30
#define INDIGO_PROFILE_BIO_MAX 320
#define INDIGO_DRAFT_MAX 1024
/* did:plc: identifiers are short, but a custom-domain did is whatever the
 * domain's PLC record allows, so leave room rather than truncating one.
 * Matches INDIGO_SEARCH_DID_MAX; the profile needs its own copy because
 * social.h must not depend on search.h. */
#define INDIGO_PROFILE_DID_MAX 128

typedef enum {
    INDIGO_NOTE_OTHER = 0,
    INDIGO_NOTE_LIKE,
    INDIGO_NOTE_REPOST,
    INDIGO_NOTE_FOLLOW,
    INDIGO_NOTE_REPLY,
    INDIGO_NOTE_MENTION,
    INDIGO_NOTE_QUOTE,
} indigo_note_kind;

typedef struct {
    indigo_note_kind kind;
    char name[INDIGO_POST_NAME_MAX];
    char handle[INDIGO_POST_NAME_MAX];
    /* Text of the reply/mention/quote; empty for likes, reposts, follows. */
    char text[INDIGO_POST_NOTE_MAX * 2];
    /* The post to open: the notification's own post for reply/mention/quote,
     * the person's own post for like/repost. Empty for follows. */
    char target_uri[INDIGO_POST_URI_MAX];
    bool unread;
} indigo_notification;

typedef struct {
    indigo_notification items[INDIGO_NOTIFICATION_MAX];
    unsigned count;
    unsigned selected;
    unsigned scroll;
    bool loading;
    char status[INDIGO_POST_NOTE_MAX];
    bool status_is_error;
} indigo_notifications;

typedef struct {
    char handle[INDIGO_POST_NAME_MAX];
    char display_name[INDIGO_POST_NAME_MAX];
    char bio[INDIGO_PROFILE_BIO_MAX];
    unsigned followers;
    unsigned follows;
    unsigned posts;
    bool following;
    /* Wolfram's getProfile gives both of these, and neither can be recovered
     * from the fields above: follow needs the subject's did, and unfollow is
     * by record URI with no handle to resolve it from. */
    char did[INDIGO_PROFILE_DID_MAX];
    char follow_uri[INDIGO_POST_URI_MAX];
    /* Mute is an account-level flag with no record URI; block is a repo record
     * deleted by URI, like follow. Both come from the same getProfile
     * response, so keeping them costs nothing once did and follow_uri are
     * here. */
    bool muted;
    bool blocked;
    char block_uri[INDIGO_POST_URI_MAX];
    /* pinnedPost.uri, or empty. getProfile already returns it, so keeping it
     * costs nothing; opening it needs no extra request, only the URI. */
    char pinned_uri[INDIGO_POST_URI_MAX];
    /* Set while a follow/unfollow is in flight. The `following` flag flips
     * immediately as an optimistic update and is put back if the job fails. */
    bool follow_busy;
    /* The same for mute and block. */
    bool mute_busy;
    bool block_busy;
    bool loaded;
    bool loading;
    char status[INDIGO_POST_NOTE_MAX];
    bool status_is_error;
} indigo_profile;

/* Mute and block are split from follow because their record shapes differ:
 * mute is an account-level flag with no URI at all, block is a repo record
 * deleted by URI, and follow is a third shape again. The enum lives here, not
 * in the session layer, because it names what the person asked for rather
 * than which Wolfram call runs; session.h includes this header. */
typedef enum {
    INDIGO_GRAPH_NONE = 0,
    INDIGO_GRAPH_MUTE,
    INDIGO_GRAPH_UNMUTE,
    INDIGO_GRAPH_BLOCK,
    INDIGO_GRAPH_UNBLOCK,
} indigo_graph_action;

typedef enum {
    INDIGO_COMPOSE_POST = 0,
    INDIGO_COMPOSE_REPLY,
    INDIGO_COMPOSE_QUOTE,
} indigo_compose_mode;

/* A post being written. Reply and quote both refer to `target`; a reply also
 * needs the thread root so it lands in the right conversation. */
typedef struct {
    indigo_compose_mode mode;
    char text[INDIGO_DRAFT_MAX];
    indigo_post target;
    char root_uri[INDIGO_POST_URI_MAX];
    char root_cid[INDIGO_POST_CID_MAX];
    bool has_target;
    bool sending;
    char status[INDIGO_POST_NOTE_MAX];
    bool status_is_error;
} indigo_compose;

void indigo_notifications_clear(indigo_notifications *n);
bool indigo_notifications_move(indigo_notifications *n, int delta, unsigned rows);
bool indigo_notifications_select(indigo_notifications *n, unsigned index, unsigned rows);
const indigo_notification *indigo_notifications_selected(const indigo_notifications *n);

/* The More menu. It is built from the post being read, so its facet targets
 * come first, then the app-wide actions. */
#define INDIGO_MENU_MAX 14
#define INDIGO_MENU_ROWS 5

typedef enum {
    INDIGO_MENU_COMPOSE = 0,
    INDIGO_MENU_NOTIFICATIONS,
    INDIGO_MENU_FIND_PEOPLE,
    INDIGO_MENU_FIND_POSTS,
    INDIGO_MENU_LISTS,
    INDIGO_MENU_MY_PROFILE,
    INDIGO_MENU_SIGN_OUT,
    INDIGO_MENU_OPEN_MENTION,
    INDIGO_MENU_SHOW_TAG,
    INDIGO_MENU_SHOW_LINK,
    INDIGO_MENU_CLOSE,
} indigo_menu_kind;

typedef struct {
    indigo_menu_kind kind;
    char label[INDIGO_POST_NAME_MAX * 2];
    char payload[INDIGO_FACET_TARGET_MAX];
} indigo_menu_item;

typedef struct {
    indigo_menu_item items[INDIGO_MENU_MAX];
    unsigned count;
    unsigned selected;
    unsigned scroll;
} indigo_menu;

/* `post` may be NULL when nothing is selected: the facet targets are then
 * left out. `account` is the signed-in handle for the profile entry. */
void indigo_menu_build(indigo_menu *m, const indigo_post *post, const char *account);
/* Move the selection, keeping it inside the `rows` visible rows. */
bool indigo_menu_move(indigo_menu *m, int delta, unsigned rows);
/* The item shown in visible row `row`, or NULL when that row is empty. */
const indigo_menu_item *indigo_menu_row(const indigo_menu *m, unsigned row, unsigned rows);

/* Compose is allowed to publish when it has text and is not already sending. */
bool indigo_compose_ready(const indigo_compose *c);
/* Quote is only offered when there is a post to quote. */
bool indigo_compose_can_toggle(const indigo_compose *c);
void indigo_compose_toggle(indigo_compose *c);

#endif
