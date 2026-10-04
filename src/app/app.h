#ifndef INDIGO_APP_H
#define INDIGO_APP_H

#include <stdbool.h>
#include <stddef.h>

#include "app/search.h"
#include "app/social.h"
#include "app/timeline.h"
#include "store/settings_codec.h"

typedef struct indigo_input indigo_input;

#define INDIGO_SERVICE_MAX 256
#define INDIGO_HANDLE_MAX 256
#define INDIGO_PASSWORD_MAX 128
#define INDIGO_STATUS_MAX 192
#define INDIGO_DEFAULT_SERVICE "https://bsky.social"

typedef enum {
    INDIGO_SCREEN_SIGNIN = 0,
    INDIGO_SCREEN_HOME,
    INDIGO_SCREEN_THREAD,
    INDIGO_SCREEN_PROFILE,
    INDIGO_SCREEN_NOTIFICATIONS,
    INDIGO_SCREEN_MENU,
    INDIGO_SCREEN_COMPOSE,
    INDIGO_SCREEN_SEARCH,
    INDIGO_SCREEN_SETTINGS,
    INDIGO_SCREEN_IMAGE,
} indigo_screen;

typedef enum {
    INDIGO_FIELD_SERVICE = 0,
    INDIGO_FIELD_HANDLE,
    INDIGO_FIELD_PASSWORD,
    INDIGO_FIELD_COUNT,
} indigo_field;

typedef enum {
    INDIGO_PHASE_IDLE = 0,
    INDIGO_PHASE_BUSY,
} indigo_phase;

/* Side effects the app wants; the platform glue performs them. */
typedef enum {
    INDIGO_REQUEST_NONE = 0,
    INDIGO_REQUEST_EDIT_FIELD,
    INDIGO_REQUEST_SIGN_IN,
    INDIGO_REQUEST_SIGN_OUT,
    INDIGO_REQUEST_TIMELINE_REFRESH,
    INDIGO_REQUEST_TIMELINE_MORE,
    INDIGO_REQUEST_LIKE,
    INDIGO_REQUEST_UNLIKE,
    INDIGO_REQUEST_REPOST,
    INDIGO_REQUEST_UNREPOST,
    INDIGO_REQUEST_THREAD,
    INDIGO_REQUEST_PROFILE,
    INDIGO_REQUEST_NOTIFICATIONS,
    INDIGO_REQUEST_EDIT_DRAFT,
    INDIGO_REQUEST_PUBLISH,
    INDIGO_REQUEST_EDIT_QUERY,
    INDIGO_REQUEST_SEARCH,
    INDIGO_REQUEST_FOLLOW,
    INDIGO_REQUEST_GRAPH,
    INDIGO_REQUEST_PEOPLE,
    INDIGO_REQUEST_POST_SEARCH,
    INDIGO_REQUEST_AUTHOR_FEED,
    INDIGO_REQUEST_LISTS,
    INDIGO_REQUEST_LIST_MEMBERS,
    INDIGO_REQUEST_FEEDS,
    INDIGO_REQUEST_FEED,
    INDIGO_REQUEST_MUTES,
    INDIGO_REQUEST_BLOCKS,
    INDIGO_REQUEST_SAVE_SETTINGS,
} indigo_request_kind;

typedef struct {
    char service[INDIGO_SERVICE_MAX];
    char handle[INDIGO_HANDLE_MAX];
    char password[INDIGO_PASSWORD_MAX];
    indigo_field focus;
    indigo_phase phase;
    /* A progress or error line. Never contains credentials. */
    char status[INDIGO_STATUS_MAX];
    bool status_is_error;
    /* Handle of the signed-in account, shown on the home screen. */
    char account[INDIGO_HANDLE_MAX];
} indigo_signin;

typedef struct {
    indigo_screen screen;
    bool quit_requested;
    bool wolfram_linked;
    indigo_signin signin;
    indigo_request_kind request;
    indigo_field request_field;
    /* Post the pending like/repost request is about; undo_uri is the record
     * to delete for UNLIKE/UNREPOST. Valid until the next request. */
    char request_post_uri[INDIGO_POST_URI_MAX];
    char request_post_cid[INDIGO_POST_CID_MAX];
    char request_undo_uri[INDIGO_POST_URI_MAX];
    /* The state INDIGO_REQUEST_FOLLOW is asking for, so main.c can turn the
     * optimistic toggle into a follow or an unfollow. */
    bool request_follow;
    /* The state INDIGO_REQUEST_GRAPH is asking for. */
    indigo_graph_action request_graph;
    /* The list INDIGO_REQUEST_PEOPLE is asking for, and whose it is. */
    indigo_search_kind request_people;
    char request_subject[INDIGO_POST_NAME_MAX];
    /* Who INDIGO_REQUEST_AUTHOR_FEED is asking about. */
    char request_actor[INDIGO_POST_NAME_MAX];
    /* Which list INDIGO_REQUEST_LIST_MEMBERS is asking for. */
    char request_list_uri[INDIGO_POST_URI_MAX];
    /* Which feed INDIGO_REQUEST_FEED is asking for, and its name for the
     * timeline title. */
    char request_feed_uri[INDIGO_POST_URI_MAX];
    char request_feed_name[INDIGO_POST_NAME_MAX];
    /* The feed the home screen is showing, empty for the plain Following
     * timeline. Outlives the request, so the title and Back survive paging. */
    char feed_uri[INDIGO_POST_URI_MAX];
    char feed_name[INDIGO_POST_NAME_MAX];
    /* Loaded at startup and kept here so a settings screen can read and change
     * it. */
    indigo_settings settings;
    unsigned settings_selected;
    indigo_timeline timeline;
    /* The post being read in the thread view and the list around it. */
    indigo_timeline thread;
    unsigned thread_focus;
    char thread_uri[INDIGO_POST_URI_MAX];
    indigo_menu menu;
    indigo_profile profile;
    indigo_notifications notifications;
    indigo_compose compose;
    indigo_search search;
    /* The image the viewer is showing, and the screen it was opened from. */
    indigo_image image;
    /* Where B goes back to; a short stack so thread -> profile -> back works. */
    indigo_screen history[6];
    unsigned history_count;
} indigo_app;

void indigo_app_init(indigo_app *app);
void indigo_app_update(indigo_app *app, const indigo_input *input);
void indigo_app_shutdown(indigo_app *app);
bool indigo_app_should_quit(const indigo_app *app);

/* Apply settings loaded from SDMC. Clamped, because app_init() zeroes the
 * struct and a zeroed text_scale is not one of the valid scales. Call once,
 * after init and before the first sign-in attempt. */
void indigo_app_set_settings(indigo_app *app, const indigo_settings *settings);

/* Returns the pending request once, then clears it. */
indigo_request_kind indigo_app_take_request(indigo_app *app,
                                            indigo_field *field);

/* The pending request without consuming it. */
indigo_request_kind indigo_app_peek_request(const indigo_app *app);

/* Store text the person entered; a rejected entry becomes a status message. */
bool indigo_app_set_field(indigo_app *app, indigo_field f, const char *text);

/* Start sign-in as if the button were pressed (no-op if incomplete or busy). */
void indigo_app_submit(indigo_app *app);

/* The list the current screen shows posts from (timeline or thread). */
indigo_timeline *indigo_app_active_list(indigo_app *app);

/* Like/repost state follows a post into every list that holds it. */
void indigo_app_set_like(indigo_app *app, const char *post_uri, const char *like_uri,
                         bool pending);
void indigo_app_set_repost(indigo_app *app, const char *post_uri, const char *repost_uri,
                           bool pending);

/* Results fed back by the platform glue. */
void indigo_app_thread_loaded(indigo_app *app, const indigo_post *posts, unsigned count,
                              unsigned focus);
void indigo_app_thread_failed(indigo_app *app, const char *message);
void indigo_app_profile_loaded(indigo_app *app, const indigo_profile *p);
void indigo_app_profile_failed(indigo_app *app, const char *message);
void indigo_app_notifications_loaded(indigo_app *app, const indigo_notification *items,
                                     unsigned count);
void indigo_app_notifications_failed(indigo_app *app, const char *message);
/* The search query came back from the keyboard. */
void indigo_app_set_query(indigo_app *app, const char *text);
void indigo_app_search_loaded(indigo_app *app, const indigo_actor *actors, unsigned count);
void indigo_app_search_failed(indigo_app *app, const char *message);
/* Open the people list for a subject's followers or following. Switching kind
 * drops the previous results rather than showing one list under the other's
 * heading, the same rule the query has. */
void indigo_app_open_people(indigo_app *app, indigo_search_kind kind, const char *subject);
/* One person's posts. Reuses the post list that post search fills. */
void indigo_app_open_author_posts(indigo_app *app, const char *actor);
/* The signed-in account's curated lists, then one list's members. Both reuse
 * the search screen; the members list reuses the actor rows. */
void indigo_app_open_lists(indigo_app *app);
void indigo_app_open_list_members(indigo_app *app, const char *list_uri, const char *name);
void indigo_app_lists_loaded(indigo_app *app, const indigo_list *lists, unsigned count);
/* The account's saved feeds, then one feed's posts on the home screen. The
 * picker reuses the search screen with the list rows; the feed itself reuses
 * the timeline, so Back lands on the picker again. */
void indigo_app_open_feeds(indigo_app *app);
void indigo_app_open_feed(indigo_app *app, const char *feed_uri, const char *name);
/* The accounts this one has muted or blocked. Read-only lists on the search
 * screen: they exist to make a mute or block visible, and undoing one is done
 * from that person's profile, which is where it was made. */
void indigo_app_open_mutes(indigo_app *app);
void indigo_app_open_blocks(indigo_app *app);
void indigo_app_open_settings(indigo_app *app);
void indigo_app_feeds_loaded(indigo_app *app, const indigo_list *feeds, unsigned count);
/* Post search results, which are posts rather than people. Kept separate from
 * the actor-loaded path because indigo_actor is the wrong type for them. */
void indigo_app_post_search_loaded(indigo_app *app, const indigo_post *posts, unsigned count);
/* Follow/unfollow. The toggle flips `following` at once and reverts it if the
 * job fails, so the button responds to the press rather than to the network. */
void indigo_app_toggle_follow(indigo_app *app);
void indigo_app_follow_done(indigo_app *app, bool following, const char *follow_uri);
void indigo_app_follow_failed(indigo_app *app, const char *message);
/* Mute/unmute and block/unblock. Mute flips optimistically like follow; block
 * is a record, so it also has to carry the URI an unblock deletes. */
void indigo_app_toggle_mute(indigo_app *app);
void indigo_app_toggle_block(indigo_app *app);
void indigo_app_graph_done(indigo_app *app, indigo_graph_action action,
                           const char *block_uri);
void indigo_app_graph_failed(indigo_app *app, indigo_graph_action action,
                             const char *message);
/* The draft text came back from the keyboard. */
void indigo_app_set_draft(indigo_app *app, const char *text);
void indigo_app_publish_done(indigo_app *app);
void indigo_app_publish_failed(indigo_app *app, const char *message);

/* The full-size image viewer. Opens the image on the selected post of whichever
 * screen asked -- the timeline, the thread, or a post in the search results --
 * and is a no-op returning false when that post has no image, which is what
 * makes the phantom hit of an undrawn button harmless. Closing goes back to
 * that screen rather than to Home, because the viewer is pushed like any other
 * screen and the history already knows where it came from. */
bool indigo_app_open_image(indigo_app *app);
void indigo_app_close_image(indigo_app *app);
/* The post whose image the viewer would open on this screen, or NULL. The
 * layout asks the same question the action does, so the button that opens the
 * viewer is drawn exactly when opening it would do something. */
const indigo_post *indigo_app_image_source(const indigo_app *app);

/* Results fed back by the platform glue. */
void indigo_app_begin_sign_in(indigo_app *app, const char *status);
void indigo_app_sign_in_succeeded(indigo_app *app, const char *account);
void indigo_app_sign_in_failed(indigo_app *app, const char *message);
void indigo_app_signed_out(indigo_app *app, const char *message);

#endif
