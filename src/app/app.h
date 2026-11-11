#ifndef INDIGO_APP_H
#define INDIGO_APP_H

#include <stdbool.h>
#include <stddef.h>

#include "gfx/canvas.h"
#include "media/media.h"
#include "atproto/chat.h"
#include "atproto/feeds.h"
#include "app/post_queue.h"
#include "app/search.h"
#include "app/social.h"
#include "app/timeline.h"
#include "store/drafts_store.h"
#include "store/settings_codec.h"
#include "update/updater.h"
#include "atproto/session.h"

#define INDIGO_TIMELINE_CACHE_MAX 20

typedef struct {
    indigo_post posts[INDIGO_TIMELINE_CACHE_MAX];
    unsigned count;
} indigo_timeline_cache;

typedef struct indigo_input indigo_input;

#define INDIGO_SERVICE_MAX 256
#define INDIGO_HANDLE_MAX 256
#define INDIGO_PASSWORD_MAX 128
#define INDIGO_STATUS_MAX 192
#define INDIGO_DEFAULT_SERVICE "https://bsky.social"
#define INDIGO_PROFILE_NAME_MAX 64



typedef enum {
    INDIGO_SCREEN_SIGNIN = 0,
    INDIGO_SCREEN_HOME,
    INDIGO_SCREEN_THREAD,
    INDIGO_SCREEN_PROFILE,
    INDIGO_SCREEN_PROFILE_EDIT,
    INDIGO_SCREEN_NOTIFICATIONS,
    INDIGO_SCREEN_MENU,
    INDIGO_SCREEN_COMPOSE,
    INDIGO_SCREEN_SEARCH,
    INDIGO_SCREEN_SETTINGS,
    INDIGO_SCREEN_IMAGE,
    INDIGO_SCREEN_UPDATE,
    INDIGO_SCREEN_MESSAGES,
    INDIGO_SCREEN_CHAT_THREAD,
    INDIGO_SCREEN_CHAT_REQUESTS,
    INDIGO_SCREEN_CHAT_JOIN_REQUESTS,
    INDIGO_SCREEN_CHAT_GROUP,
    INDIGO_SCREEN_QUEUE,
    INDIGO_SCREEN_DRAFTS,
    INDIGO_SCREEN_BOOKMARKS,
    INDIGO_SCREEN_CREDITS,
    /* Not a screen: the number of entries above. */
    INDIGO_SCREEN_COUNT,
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

/* What the two group text fields are being collected for. */
typedef enum {
    INDIGO_CHAT_GROUP_EDIT_NONE = 0,
    INDIGO_CHAT_GROUP_EDIT_CREATE_NAME,
    INDIGO_CHAT_GROUP_EDIT_CREATE_MEMBERS,
    INDIGO_CHAT_GROUP_EDIT_RENAME,
    INDIGO_CHAT_GROUP_EDIT_ADD,
} indigo_chat_group_edit;

/* Side effects the app wants; the platform glue performs them. */
typedef enum {
    INDIGO_REQUEST_NONE = 0,
    INDIGO_REQUEST_EDIT_FIELD,
    INDIGO_REQUEST_SIGN_IN,
    INDIGO_REQUEST_SIGN_OUT,
    INDIGO_REQUEST_TIMELINE_REFRESH,
    INDIGO_REQUEST_TIMELINE_MORE,
    INDIGO_REQUEST_BOOKMARKS,
    INDIGO_REQUEST_BOOKMARKS_MORE,
    INDIGO_REQUEST_BOOKMARK,
    INDIGO_REQUEST_LIKE,
    INDIGO_REQUEST_UNLIKE,
    INDIGO_REQUEST_REPOST,
    INDIGO_REQUEST_UNREPOST,
    INDIGO_REQUEST_DELETE_POST,
    INDIGO_REQUEST_REPORT,
    INDIGO_REQUEST_THREAD_MUTE,
    INDIGO_REQUEST_THREAD,
    INDIGO_REQUEST_PROFILE,
    INDIGO_REQUEST_NOTIFICATIONS,
    INDIGO_REQUEST_NOTIFICATIONS_MORE,
    INDIGO_REQUEST_EDIT_DRAFT,
    INDIGO_REQUEST_EDIT_IMAGE_ALT,
    INDIGO_REQUEST_PUBLISH,
    INDIGO_REQUEST_EDIT_QUERY,
    INDIGO_REQUEST_SEARCH,
    INDIGO_REQUEST_FOLLOW,
    INDIGO_REQUEST_CHAT_INBOX,
    INDIGO_REQUEST_CHAT_REQUESTS,
    INDIGO_REQUEST_CHAT_REQUESTS_MORE,
    INDIGO_REQUEST_CHAT_THREAD,
    INDIGO_REQUEST_EDIT_REPLY,
    INDIGO_REQUEST_CHAT_SEND,
    INDIGO_REQUEST_EDIT_CHAT_TARGET,
    INDIGO_REQUEST_CHAT_START,
    INDIGO_REQUEST_CHAT_ACCEPT,
    INDIGO_REQUEST_CHAT_DECLINE,
    INDIGO_REQUEST_CHAT_JOIN_REQUESTS,
    INDIGO_REQUEST_CHAT_JOIN_REQUESTS_MORE,
    INDIGO_REQUEST_CHAT_JOIN_APPROVE,
    INDIGO_REQUEST_CHAT_JOIN_REJECT,
    INDIGO_REQUEST_EDIT_CHAT_GROUP_NAME,
    INDIGO_REQUEST_EDIT_CHAT_GROUP_MEMBERS,
    INDIGO_REQUEST_CHAT_GROUP_INFO,
    INDIGO_REQUEST_CHAT_GROUP_CREATE,
    INDIGO_REQUEST_CHAT_GROUP_RENAME,
    INDIGO_REQUEST_CHAT_GROUP_ADD,
    INDIGO_REQUEST_CHAT_GROUP_REMOVE,
    INDIGO_REQUEST_CHAT_JOIN_LINK,
    INDIGO_REQUEST_GRAPH,
    INDIGO_REQUEST_PEOPLE,
    INDIGO_REQUEST_POST_SEARCH,
    INDIGO_REQUEST_AUTHOR_FEED,
    INDIGO_REQUEST_LISTS,
    INDIGO_REQUEST_LIST_MEMBERS,
    INDIGO_REQUEST_EDIT_LIST_NAME,
    INDIGO_REQUEST_EDIT_LIST_DESC,
    INDIGO_REQUEST_EDIT_LIST_SUBJECT,
    INDIGO_REQUEST_LIST_SAVE,
    INDIGO_REQUEST_LIST_ADD,
    INDIGO_REQUEST_LIST_REMOVE,
    INDIGO_REQUEST_STARTER_PACK,
    INDIGO_REQUEST_FEEDS,
    INDIGO_REQUEST_FEED,
    INDIGO_REQUEST_MUTES,
    INDIGO_REQUEST_BLOCKS,
    INDIGO_REQUEST_EDIT_PROFILE_NAME,
    INDIGO_REQUEST_EDIT_PROFILE_BIO,
    INDIGO_REQUEST_PROFILE_UPDATE,
    INDIGO_REQUEST_SAVE_SETTINGS,
    INDIGO_REQUEST_UPDATE_CHECK,
    INDIGO_REQUEST_UPDATE_INSTALL,
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
    const char *url;
    unsigned width;
    unsigned height;
    const char *alt_text;
} indigo_image_view;

typedef struct {
    char text[INDIGO_POST_TEXT_MAX];
    char reply_root[INDIGO_POST_URI_MAX];
    char reply_parent[INDIGO_POST_URI_MAX];
    char quote_uri[INDIGO_POST_URI_MAX];
    bool has_image;
} indigo_draft;

typedef struct {
    indigo_draft records[8];
    unsigned count;
} indigo_drafts;

typedef enum {
    INDIGO_PROFILE_TAB_POSTS = 0,
    INDIGO_PROFILE_TAB_REPLIES,
    INDIGO_PROFILE_TAB_MEDIA,
    INDIGO_PROFILE_TAB_LIKES,
    INDIGO_PROFILE_TAB_COUNT,
} indigo_profile_tab;

typedef struct {
    char uri[INDIGO_POST_URI_MAX];
    char name[INDIGO_PROFILE_NAME_MAX];
    char purpose[INDIGO_PROFILE_BIO_MAX];
} indigo_list_summary;

typedef struct {
    indigo_list_summary lists[30];
    unsigned count;
} indigo_list_summary_list;

#include "app/search.h"

typedef struct {
    indigo_actor actors[30];
    unsigned count;
} indigo_actor_list;

typedef struct {
    indigo_screen screen;
    bool quit;
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
    /* The state INDIGO_REQUEST_THREAD_MUTE is asking for. The root travels in
     * request_post_uri. */
    bool request_thread_mute;
    /* INDIGO_REQUEST_REPORT: the account target, and the reason. A post
     * target travels in request_post_uri/request_post_cid. */
    bool request_report_account;
    char request_report_did[INDIGO_PROFILE_DID_MAX];
    char request_report_reason[INDIGO_REPORT_REASON_MAX];
    /* The state INDIGO_REQUEST_BOOKMARK is asking for. */
    bool request_bookmark;
    /* The state INDIGO_REQUEST_GRAPH is asking for. */
    indigo_graph_action request_graph;
    /* The list INDIGO_REQUEST_PEOPLE is asking for, and whose it is. */
    indigo_search_kind request_people;
    char request_subject[INDIGO_POST_URI_MAX];
    /* Who INDIGO_REQUEST_AUTHOR_FEED is asking about. */
    char request_actor[INDIGO_POST_NAME_MAX];
    /* Which list INDIGO_REQUEST_LIST_MEMBERS is asking for. */
    char request_list_uri[INDIGO_POST_URI_MAX];
    /* Which pack INDIGO_REQUEST_STARTER_PACK is asking for. */
    char request_starter_pack_uri[INDIGO_POST_URI_MAX];
    /* Which feed INDIGO_REQUEST_FEED is asking for, and its name for the
     * timeline title. */
    char request_feed_uri[INDIGO_POST_URI_MAX];
    char request_feed_name[INDIGO_POST_NAME_MAX];
    /* The feed the home screen is showing, empty for the plain Following
     * timeline. Outlives the request, so the title and Back survive paging. */
    char feed_uri[INDIGO_POST_URI_MAX];
    char feed_name[INDIGO_POST_NAME_MAX];
    /* The post the thread screen is showing. */
    char thread_uri[INDIGO_POST_URI_MAX];
    char thread_root[INDIGO_POST_URI_MAX];
    bool thread_muted;
    /* The profile the profile screen is showing. */
    char profile_did[INDIGO_PROFILE_DID_MAX];
    /* The profile the profile-edit screen is showing. */
    char profile_edit_did[INDIGO_PROFILE_DID_MAX];
    char profile_edit_name[INDIGO_PROFILE_NAME_MAX];
    char profile_edit_bio[INDIGO_PROFILE_BIO_MAX];
    /* The notifications list. */
    indigo_notifications notifications;
    /* The search query and results. */
    char search_query[INDIGO_SEARCH_QUERY_MAX];
    indigo_search_kind search_kind;
    indigo_search search;
    /* The compose screen. */
    indigo_compose compose;
    /* The timeline. */
    indigo_timeline timeline;
    /* The bookmarks list. */
    indigo_timeline bookmarks;
    /* The image viewer. */
    indigo_image_view image_view;
    /* The update screen. */
    indigo_updater updater;
    /* The messages screen. */
    indigo_chat_inbox messages;
    /* The conversation the chat-thread screen is showing. */
    indigo_chat_thread chat_thread;
    /* The conversation the chat-requests screen is showing. */
    indigo_chat_inbox chat_requests;
    /* The join requests the chat-join-requests screen is showing. */
    indigo_chat_join_requests chat_join_requests;
    /* The group the chat-group screen is showing. */
    indigo_chat_group chat_group;
    /* The group the chat-group-edit screen is editing. */
    indigo_chat_group_edit chat_group_edit;
    /* The queue screen. */
    indigo_post_queue queue;
    /* The drafts screen. */
    indigo_saved_drafts drafts;
    unsigned draft_selected;
    bool draft_delete_armed;
    char draft_status[INDIGO_POST_NOTE_MAX];
    bool draft_status_is_error;

    /* Chat requests */
    bool chat_decision_pending;
    char chat_decision_id[INDIGO_CHAT_ID_MAX];
    char chat_decision_name[INDIGO_POST_NAME_MAX];
    bool chat_decision_accept;
    indigo_chat_inbox requests;
    indigo_chat_join_requests join_requests;
    int requests_count;

    /* Group management */
    indigo_chat_group group;
    indigo_chat_group_edit group_edit;
    bool group_pending;
    char group_name[INDIGO_CHAT_GROUP_NAME_MAX];
    char group_members[INDIGO_CHAT_GROUP_MEMBERS_TEXT_MAX];
    char group_status[INDIGO_POST_NOTE_MAX];
    bool group_status_is_error;

    /* Chat state */
    bool chat_is_group;
    bool chat_start_pending;
    char chat_target[INDIGO_CHAT_TARGET_MAX];

    /* Thread and post management */
    indigo_timeline thread;
    bool confirm_delete;
    char delete_uri[INDIGO_POST_URI_MAX];

    /* Queue management */
    unsigned queue_selected;
    bool queue_discard_armed;
    bool queue_send_pending;
    indigo_queued_post queue_inflight;
    char queue_status[INDIGO_POST_NOTE_MAX];
    bool queue_status_is_error;

    /* Profile management */
    indigo_profile profile;
    indigo_profile_edit profile_edit;

    /* History management */
    unsigned history_count;
    indigo_screen history[INDIGO_SCREEN_COUNT];

    /* Thread management */
    unsigned thread_focus;
    char thread_muted_root[INDIGO_POST_URI_MAX];

    /* Settings management */
    unsigned settings_selected;

    /* Menu management */
    indigo_menu menu;
    char images_dir[256];
    char camera_dir[256];

    /* Image viewer */
    indigo_image image;

    /* Quit flag */
    bool quit_requested;


    /* The settings screen. */
    indigo_settings settings;
    /* The profile tab the profile screen is showing. */
    indigo_profile_tab profile_tab;
    /* The list the list screen is showing. */
    indigo_list_summary_list lists;
    /* The list members the list-members screen is showing. */
    indigo_actor_list list_members;
    /* The starter pack the starter-pack screen is showing. */
    indigo_starter_pack starter_pack;
    /* The starter pack members the starter-pack-members screen is showing. */
    indigo_actor_list starter_pack_members;
    /* The people list the people screen is showing. */
    indigo_actor_list people;
    /* The post search the post-search screen is showing. */
    indigo_timeline post_search;
    /* The author feed the author-feed screen is showing. */
    indigo_timeline author_feed;
    /* The muted list the mutes screen is showing. */
    indigo_actor_list mutes;
    /* The blocked list the blocks screen is showing. */
    indigo_actor_list blocks;
    /* The feeds list the feeds screen is showing. */
    indigo_list_summary_list feeds;
    /* The feed the feed screen is showing. */
    indigo_timeline feed;
} indigo_app;

/* Initialise an app. */
/* Timeline cache functions */
const indigo_post *indigo_timeline_posts(const indigo_timeline *timeline, unsigned *count);
bool indigo_timeline_cache_append(indigo_timeline_cache *cache, const indigo_post *post);
indigo_store_status indigo_timeline_cache_save(const indigo_timeline_cache *cache, const char *path);
indigo_store_status indigo_timeline_cache_load(indigo_timeline_cache *cache, const char *path);

/* Draft functions */
void indigo_app_set_draft(indigo_app *app, const char *text);
void indigo_app_queue_append(indigo_app *app, const char *text);
void indigo_app_drafts_append(indigo_app *app, const char *text);
void indigo_app_update_drafts(indigo_app *app, const indigo_input *input);
indigo_store_status indigo_draft_load(indigo_draft *draft, const char *path);
indigo_store_status indigo_post_queue_load(indigo_post_queue *queue, const char *path);
indigo_store_status indigo_drafts_load(indigo_drafts *drafts, const char *path);

/* Settings functions */
void indigo_app_set_settings(indigo_app *app, const indigo_settings *settings);
indigo_store_status indigo_settings_load(indigo_settings *settings, const char *path);

/* File functions */
indigo_store_status indigo_file_remove(const char *path);

/* App lifecycle functions */
void indigo_app_init(indigo_app *app);
void indigo_app_update(indigo_app *app, const indigo_input *input);
void indigo_app_draw(indigo_app *app, indigo_canvas *canvas);
void indigo_app_shutdown(indigo_app *app);
void shutdown_app(indigo_app *app);

/* Media functions */
void indigo_media_init(indigo_media_cache *cache);
void indigo_media_shutdown(void);

/* UI functions */
void indigo_ui_render(indigo_canvas *canvas);
void indigo_canvas_reset(indigo_canvas *canvas);
void indigo_canvas_init(indigo_canvas *canvas, int width, int height);

/* Post functions */
bool indigo_app_open_image(indigo_app *app);
bool indigo_app_selected_post_is_own(const indigo_app *app);
void indigo_app_toggle_thread_mute(indigo_app *app);
void indigo_app_toggle_follow(indigo_app *app);
void indigo_app_toggle_mute(indigo_app *app);
void indigo_app_toggle_block(indigo_app *app);
void indigo_app_open_people(indigo_app *app, indigo_search_kind kind, const char *subject);
void indigo_app_open_author_posts(indigo_app *app, const char *actor);
bool indigo_app_profile_is_self(const indigo_app *app);
void indigo_app_open_profile_edit(indigo_app *app);

/* Timeline functions */
indigo_timeline *indigo_app_active_list(indigo_app *app);
void indigo_app_set_bookmarked(indigo_app *app, const char *post_uri, bool bookmarked);

/* Queue functions */
bool indigo_app_queue_publish_done(indigo_app *app);
bool indigo_app_queue_publish_failed(indigo_app *app, const char *message);

/* Screen functions */
void indigo_app_open_starter_pack(indigo_app *app, const char *uri);
void indigo_app_open_lists(indigo_app *app);
void indigo_app_open_feeds(indigo_app *app);
void indigo_app_open_bookmarks(indigo_app *app);
void indigo_app_toggle_bookmark(indigo_app *app);
void indigo_app_open_mutes(indigo_app *app);
void indigo_app_open_blocks(indigo_app *app);
void indigo_app_open_queue(indigo_app *app);
void indigo_app_open_drafts(indigo_app *app);
void indigo_app_open_settings(indigo_app *app);
void indigo_app_open_credits(indigo_app *app);
void indigo_app_open_update(indigo_app *app);
void indigo_app_open_list_members(indigo_app *app, const char *uri, const char *name);
void indigo_app_open_feed(indigo_app *app, const char *uri, const char *name);
void indigo_app_list_edit_begin(indigo_app *app, bool create);
void indigo_app_next_author_tab(indigo_app *app);
void indigo_app_save_compose_later(indigo_app *app);
void indigo_app_save_compose_draft(indigo_app *app);
void indigo_app_close_image(indigo_app *app);

/* Update functions */
void indigo_app_update_profile_edit(indigo_app *app, const indigo_input *input);
void indigo_app_update_queue(indigo_app *app, const indigo_input *input);
void indigo_app_update_bookmarks(indigo_app *app, const indigo_input *input);
void indigo_app_update_chat_requests(indigo_app *app, const indigo_input *input);
void indigo_app_update_chat_join_requests(indigo_app *app, const indigo_input *input);

/* Thread functions */
bool indigo_app_thread_muted(const indigo_app *app);

/* Profile functions */
int indigo_app_author_tab_after(const indigo_app *app);

/* Image functions */
const indigo_post *indigo_app_image_source(const indigo_app *app);

/* Update the app's state. */
void indigo_app_update(indigo_app *app, const indigo_input *input);

/* Include necessary headers for types */
#include "gfx/canvas.h"
#include "media/media.h"
#include "atproto/feeds.h"

/* Draw the app. */
void indigo_app_draw(indigo_app *app, indigo_canvas *canvas);

/* Handle input while a request is active. */
void indigo_app_handle_request_input(indigo_app *app, const indigo_input *input);

/* Handle input while no request is active. */
void indigo_app_handle_input(indigo_app *app, const indigo_input *input);

/* Sign-in succeeded. */
void indigo_app_sign_in_succeeded(indigo_app *app, const char *account);

/* Sign-in failed. */
void indigo_app_sign_in_failed(indigo_app *app, const char *message);

/* Signed out. */
void indigo_app_signed_out(indigo_app *app, const char *message);

/* Timeline loaded. */
void indigo_app_timeline_loaded(indigo_app *app, const indigo_post *posts, unsigned count,
                               const char *cursor);

/* Timeline failed. */
void indigo_app_timeline_failed(indigo_app *app, const char *message);

/* Bookmarks loaded. */
void indigo_app_bookmarks_loaded(indigo_app *app, const indigo_post *posts, unsigned count,
                                const char *cursor);

/* Bookmarks failed. */
void indigo_app_bookmarks_failed(indigo_app *app, const char *message);

/* Bookmark succeeded. */
void indigo_app_bookmark_done(indigo_app *app, const char *post_uri, bool bookmarked);

/* Bookmark failed. */
void indigo_app_bookmark_failed(indigo_app *app, const char *message);

/* Like/repost succeeded. */
void indigo_app_set_like(indigo_app *app, const char *post_uri, const char *record_uri,
                         bool failed);
void indigo_app_set_repost(indigo_app *app, const char *post_uri, const char *record_uri,
                           bool failed);

/* Like/repost failed. */
void indigo_app_action_failed(indigo_app *app, const char *message);

/* Post deleted. */
void indigo_app_post_deleted(indigo_app *app, const char *post_uri);

/* Post deletion failed. */
void indigo_app_post_delete_failed(indigo_app *app, const char *message);

/* Thread loaded. */
void indigo_app_thread_loaded(indigo_app *app, const indigo_post *posts, unsigned count,
                             unsigned focus, const char *root_uri);

/* Thread failed. */
void indigo_app_thread_failed(indigo_app *app, const char *message);

/* Thread mute succeeded. */
void indigo_app_thread_mute_done(indigo_app *app, const char *root_uri, bool muted);

/* Thread mute failed. */
void indigo_app_thread_mute_failed(indigo_app *app, const char *message);

/* Report succeeded. */
void indigo_app_report_done(indigo_app *app);

/* Report failed. */
void indigo_app_report_failed(indigo_app *app, const char *message);

/* Profile loaded. */
void indigo_app_profile_loaded(indigo_app *app, const indigo_profile *profile);

/* Profile failed. */
void indigo_app_profile_failed(indigo_app *app, const char *message);

/* Profile update succeeded. */
void indigo_app_profile_update_done(indigo_app *app);

/* Profile update failed. */
void indigo_app_profile_update_failed(indigo_app *app, const char *message);

/* Notifications loaded. */
void indigo_app_notifications_loaded(indigo_app *app, const indigo_notification *items,
                                    unsigned count, const char *cursor);

/* Notifications failed. */
void indigo_app_notifications_failed(indigo_app *app, const char *message);

/* Published. */
void indigo_app_published(indigo_app *app, indigo_compose_mode mode, const char *post_uri,
                         bool partial, unsigned posted_count);

/* Publish failed. */
void indigo_app_publish_failed(indigo_app *app, const char *message);

/* Search loaded. */
void indigo_app_search_loaded(indigo_app *app, const indigo_actor *actors, unsigned count,
                             const char *cursor);

/* Search failed. */
void indigo_app_search_failed(indigo_app *app, const char *message);

/* Post search loaded. */
void indigo_app_post_search_loaded(indigo_app *app, const indigo_post *posts, unsigned count,
                                  const char *cursor);

/* Lists loaded. */
void indigo_app_lists_loaded(indigo_app *app, const indigo_list *lists, unsigned count,
                           const char *cursor);

/* List write succeeded. */
void indigo_app_list_write_done(indigo_app *app, const char *message);

/* List write failed. */
void indigo_app_list_write_failed(indigo_app *app, const char *message);

/* Feeds loaded. */
void indigo_app_feeds_loaded(indigo_app *app, const indigo_list *feeds, unsigned count,
                           const char *cursor);

/* Starter pack loaded. */
void indigo_app_starter_pack_loaded(indigo_app *app, const indigo_starter_pack *pack,
                                    const indigo_actor *actors, unsigned count,
                                    const char *cursor);

/* Starter pack failed. */
void indigo_app_starter_pack_failed(indigo_app *app, const char *message);

/* Follow succeeded. */
void indigo_app_follow_done(indigo_app *app, bool following, const char *follow_uri);

/* Follow failed. */
void indigo_app_follow_failed(indigo_app *app, const char *message);

/* Graph action succeeded. */
void indigo_app_graph_done(indigo_app *app, indigo_graph_action action, const char *block_uri);

/* Graph action failed. */
void indigo_app_graph_failed(indigo_app *app, indigo_graph_action action, const char *message);

/* Chat inbox loaded. */
void indigo_app_chat_loaded(indigo_app *app, const indigo_chat_convo *convos, unsigned count,
                           const char *cursor);

/* Chat inbox failed. */
void indigo_app_chat_failed(indigo_app *app, const char *message);

/* Chat thread loaded. */
void indigo_app_chat_thread_loaded(indigo_app *app, const indigo_chat_thread *src);

/* Chat thread failed. */
void indigo_app_chat_thread_failed(indigo_app *app, const char *message);

/* Chat send succeeded. */
void indigo_app_chat_send_done(indigo_app *app);

/* Chat send failed. */
void indigo_app_chat_send_failed(indigo_app *app, const char *message);

/* Chat requests loaded. */
void indigo_app_chat_requests_loaded(indigo_app *app, const indigo_chat_join_request *requests,
                                    unsigned count, const char *cursor);

/* Chat requests failed. */
void indigo_app_chat_requests_failed(indigo_app *app, const char *message);

/* Chat request accepted. */
void indigo_app_chat_accept_done(indigo_app *app);

/* Chat request accept failed. */
void indigo_app_chat_accept_failed(indigo_app *app, const char *message);

/* Chat request declined. */
void indigo_app_chat_decline_done(indigo_app *app);

/* Chat request decline failed. */
void indigo_app_chat_decline_failed(indigo_app *app, const char *message);

/* Chat start succeeded. */
void indigo_app_chat_start_done(indigo_app *app, const char *convo_id);

/* Chat start failed. */
void indigo_app_chat_start_failed(indigo_app *app, const char *message);

/* Chat join requests loaded. */
void indigo_app_chat_join_requests_loaded(indigo_app *app,
                                          const indigo_chat_join_request *requests,
                                          unsigned count, const char *cursor);

/* Chat join requests failed. */
void indigo_app_chat_join_requests_failed(indigo_app *app, const char *message);

/* Chat join request approved. */
void indigo_app_chat_join_request_done(indigo_app *app, bool approved);

/* Chat join request approval failed. */
void indigo_app_chat_join_request_failed(indigo_app *app, const char *message);

/* Chat group info loaded. */
void indigo_app_chat_group_info_loaded(indigo_app *app, const indigo_chat_group *group);

/* Chat group info failed. */
void indigo_app_chat_group_info_failed(indigo_app *app, const char *message);

/* Chat group created. */
void indigo_app_chat_group_create_done(indigo_app *app, const char *convo_id);

/* Chat group creation failed. */
void indigo_app_chat_group_create_failed(indigo_app *app, const char *message);

/* Chat group renamed. */
void indigo_app_chat_group_rename_done(indigo_app *app);

/* Chat group rename failed. */
void indigo_app_chat_group_rename_failed(indigo_app *app, const char *message);

/* Chat group member added. */
void indigo_app_chat_group_add_done(indigo_app *app);

/* Chat group member add failed. */
void indigo_app_chat_group_add_failed(indigo_app *app, const char *message);

/* Chat group member removed. */
void indigo_app_chat_group_remove_done(indigo_app *app);

/* Chat group member remove failed. */
void indigo_app_chat_group_remove_failed(indigo_app *app, const char *message);

/* Chat join link created or toggled. */
void indigo_app_chat_join_link_done(indigo_app *app);

/* Chat join link creation failed. */
void indigo_app_chat_join_link_failed(indigo_app *app, const char *message);

/* Feed discovery loaded. */
void indigo_app_feed_discovery_loaded(indigo_app *app, const indigo_feed_discovery_page *page);

/* Feed discovery failed. */
void indigo_app_feed_discovery_failed(indigo_app *app, const char *message);

/* Feed generator loaded. */
void indigo_app_feed_generator_loaded(indigo_app *app, const indigo_feed_generator *generator);

/* Feed generator failed. */
void indigo_app_feed_generator_failed(indigo_app *app, const char *message);

/* Saved feeds loaded. */
void indigo_app_saved_feeds_loaded(indigo_app *app, const indigo_saved_feeds_v2 *feeds);

/* Saved feeds failed. */
void indigo_app_saved_feeds_failed(indigo_app *app, const char *message);

/* Feed saved. */
void indigo_app_save_feed_done(indigo_app *app);

/* Feed save failed. */
void indigo_app_save_feed_failed(indigo_app *app, const char *message);

/* Feed removed. */
void indigo_app_remove_feed_done(indigo_app *app);

/* Feed removal failed. */
void indigo_app_remove_feed_failed(indigo_app *app, const char *message);

/* Feed pinned. */
void indigo_app_pin_feed_done(indigo_app *app);

/* Feed pin failed. */
void indigo_app_pin_feed_failed(indigo_app *app, const char *message);

/* Set the updater. */
void indigo_app_set_updater(indigo_app *app, const char *describe, bool can_swap);

/* Set the available update version. */
void indigo_app_set_update_available(indigo_app *app, const char *version);

/* Set the update progress. */
void indigo_app_set_update_progress(indigo_app *app, unsigned progress);

/* Set the update error. */
void indigo_app_set_update_error(indigo_app *app, const char *message);

/* Set the undo record. */
void indigo_app_set_undo_record(indigo_app *app, const char *post_uri, const char *undo_uri);

/* Action undone. */
void indigo_app_action_undone(indigo_app *app, const char *verb, const char *post_uri);


/* Session, request and result entry points (platform glue and main.c). */
/* Results fed back by the platform glue. */
void indigo_app_begin_sign_in(indigo_app *app, const char *status);

void indigo_app_chat_group_done(indigo_app *app);

void indigo_app_chat_group_failed(indigo_app *app, const char *message);

void indigo_app_chat_join_decision_done(indigo_app *app, bool approved);

void indigo_app_chat_join_decision_failed(indigo_app *app, const char *message);

void indigo_app_chat_request_done(indigo_app *app, bool accepted);

void indigo_app_chat_request_failed(indigo_app *app, const char *message);

void indigo_app_delete_done(indigo_app *app, const char *post_uri);

void indigo_app_delete_failed(indigo_app *app, const char *message);

void indigo_app_group_failed(indigo_app *app, const char *message);

void indigo_app_group_loaded(indigo_app *app, const indigo_chat_group *src);

void indigo_app_join_requests_failed(indigo_app *app, const char *message);

void indigo_app_join_requests_loaded(indigo_app *app,
                                     const indigo_chat_join_requests *src);

/* Validates the working copy and leaves it ready for main.c to submit. */
bool indigo_app_list_save(indigo_app *app);

void indigo_app_list_set_add_subject(indigo_app *app, const char *text);

void indigo_app_list_set_description(indigo_app *app, const char *text);

void indigo_app_list_set_name(indigo_app *app, const char *text);

void indigo_app_messages_failed(indigo_app *app, const char *message);

void indigo_app_messages_loaded(indigo_app *app, const indigo_chat_inbox *src);

void indigo_app_open_join_requests(indigo_app *app);

void indigo_app_open_requests(indigo_app *app);

/* The pending request without consuming it. */
indigo_request_kind indigo_app_peek_request(const indigo_app *app);

void indigo_app_persist_queue(indigo_app *app);

bool indigo_app_profile_edit_valid(const char *display_name, const char *bio);

void indigo_app_publish_done(indigo_app *app);

void indigo_app_publish_partial(indigo_app *app, const char *message);

void indigo_app_queue_send(indigo_app *app, unsigned index);

void indigo_app_requests_failed(indigo_app *app, const char *message);

void indigo_app_requests_loaded(indigo_app *app, const indigo_chat_inbox *src);

void indigo_app_set_chat_group_members(indigo_app *app, const char *text);

void indigo_app_set_chat_group_name(indigo_app *app, const char *text);

/* Starting a conversation, and settling message requests and join requests.
 * The failure paths take the event's own message when it has one, because the
 * server's refusal (an unknown handle, your own account) is more useful than
 * a generic wording. */
void indigo_app_set_chat_target(indigo_app *app, const char *text);

/* Store text the person entered; a rejected entry becomes a status message. */
bool indigo_app_set_field(indigo_app *app, indigo_field f, const char *text);

/* The alt text for the attached image came back from the keyboard. */
void indigo_app_set_image_alt(indigo_app *app, const char *text);

void indigo_app_set_profile_bio(indigo_app *app, const char *text);

void indigo_app_set_profile_name(indigo_app *app, const char *text);

/* The search query came back from the keyboard. */
void indigo_app_set_query(indigo_app *app, const char *text);

bool indigo_app_should_quit(const indigo_app *app);

/* Start sign-in as if the button were pressed (no-op if incomplete or busy). */
void indigo_app_submit(indigo_app *app);

/* Returns the pending request once, then clears it. */
indigo_request_kind indigo_app_take_request(indigo_app *app,
                                            indigo_field *field);

#endif
