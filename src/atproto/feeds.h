/*
 * feeds.h -- custom feed discovery and management.
 *
 * Feed discovery uses app.bsky.unspecced.getSuggestedFeeds (via
 * wf_agent_get_suggested_feeds_typed) to find popular generators.
 * Feed management reads/writes savedFeedsPrefV2 via
 * wf_agent_get_actor_prefs_typed / wf_agent_put_actor_prefs_typed.
 */

#ifndef INDIGO_FEEDS_H
#define INDIGO_FEEDS_H

#include "app/timeline.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of discovered feed generators per page. */
#define INDIGO_FEED_DISCOVERY_PAGE 25

/* Maximum number of saved feeds (matches savedFeedsPrefV2 limit). */
#define INDIGO_SAVED_FEEDS_MAX 25

/* DID maximum length (matches other DID constants in the codebase). */
#define INDIGO_DID_MAX 128

/* A feed (app.bsky.feed.defs#feedViewPost). */
typedef struct {
    char uri[INDIGO_POST_URI_MAX];
    char name[INDIGO_POST_NAME_MAX];
    char avatar[INDIGO_MEDIA_URL_MAX];
} indigo_feed;

/* A discovered feed generator (app.bsky.feed.defs#generatorView). */
typedef struct {
    char uri[INDIGO_POST_URI_MAX];
    char did[INDIGO_DID_MAX];
    char display_name[INDIGO_POST_NAME_MAX];
    char description[INDIGO_POST_NOTE_MAX];
    char avatar[INDIGO_MEDIA_URL_MAX];
    bool has_like_count;
    int64_t like_count;
    bool has_viewer_like;
    bool viewer_like;
    bool has_subscriber_count;
    int64_t subscriber_count;
    bool has_viewer_subscribed;
    bool viewer_subscribed;
} indigo_feed_generator;

/* A page of discovered feed generators. */
typedef struct {
    int count;
    bool has_more;
    char cursor[INDIGO_CURSOR_MAX];
    indigo_feed_generator generators[INDIGO_FEED_DISCOVERY_PAGE];
} indigo_feed_discovery_page;

/* A saved feed entry (from savedFeedsPrefV2#items where type == "feed"). */
typedef struct {
    char uri[INDIGO_POST_URI_MAX];
    char display_name[INDIGO_POST_NAME_MAX];
    bool pinned;
} indigo_saved_feed_entry;

/* The account's saved feeds list (savedFeedsPrefV2). */
typedef struct {
    int count;
    indigo_saved_feed_entry entries[INDIGO_SAVED_FEEDS_MAX];
} indigo_saved_feeds_v2;

/* Submission functions. */
bool indigo_session_submit_feed_discovery(const char *cursor);
bool indigo_session_submit_feed_generator(const char *feed_uri);
bool indigo_session_submit_saved_feeds_v2(void);
bool indigo_session_submit_save_feed(const char *feed_uri, bool pinned);
bool indigo_session_submit_remove_feed(const char *feed_uri);
bool indigo_session_submit_pin_feed(const char *feed_uri, bool pinned);

#ifdef __cplusplus
}
#endif

#endif /* INDIGO_FEEDS_H */
