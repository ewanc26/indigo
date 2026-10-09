/* What the platform glue calls when a request finishes, and the calls that open the lists and screens a request fills. */

#include "app/app_internal.h"

#include <strings.h>
#include <wolfram/profile_tab.h>

void
indigo_app_begin_sign_in(indigo_app *app, const char *status)
{
    app->signin.phase = INDIGO_PHASE_BUSY;
    indigo_app_set_status(&app->signin, status, false);
}

void
indigo_app_sign_in_succeeded(indigo_app *app, const char *account)
{
    indigo_signin *s = &app->signin;

    s->phase = INDIGO_PHASE_IDLE;
    memset(s->password, 0, sizeof s->password);
    strncpy(s->account, account ? account : "", sizeof s->account - 1);
    s->account[sizeof s->account - 1] = '\0';
    indigo_app_set_status(s, "", false);
    app->screen = INDIGO_SCREEN_HOME;
    app->history_count = 0;
    indigo_timeline_begin_fetch(&app->timeline, true);

    /* A default feed replaces the Following timeline for the first screen
     * only. indigo_app_open_feed() is the wrong entry point here: it pushes a
     * history entry, which at startup would send B from Home back to sign-in.
     * The name is the record key's until the feed is named by a later lookup;
     * the picker is where a named title comes from. */
    if (app->settings.default_feed[0]) {
        indigo_copy_utf8(app->feed_uri, sizeof app->feed_uri,
                         app->settings.default_feed);
        indigo_copy_utf8(app->feed_name, sizeof app->feed_name, "Feed");
        indigo_copy_utf8(app->request_feed_uri, sizeof app->request_feed_uri,
                         app->settings.default_feed);
        indigo_copy_utf8(app->request_feed_name, sizeof app->request_feed_name,
                         "Feed");
        app->request = INDIGO_REQUEST_FEED;
        return;
    }
    app->request = INDIGO_REQUEST_TIMELINE_REFRESH;
}

void
indigo_app_sign_in_failed(indigo_app *app, const char *message)
{
    app->signin.phase = INDIGO_PHASE_IDLE;
    indigo_app_set_status(&app->signin, message, true);
    app->screen = INDIGO_SCREEN_SIGNIN;
}

void
indigo_app_signed_out(indigo_app *app, const char *message)
{
    indigo_signin *s = &app->signin;

    s->phase = INDIGO_PHASE_IDLE;
    s->account[0] = '\0';
    memset(s->password, 0, sizeof s->password);
    indigo_timeline_clear(&app->timeline);
    indigo_timeline_clear(&app->thread);
    indigo_notifications_clear(&app->notifications);
    memset(&app->profile, 0, sizeof app->profile);
    app->history_count = 0;
    indigo_app_set_status(s, message, false);
    app->screen = INDIGO_SCREEN_SIGNIN;
}

void
indigo_app_thread_loaded(indigo_app *app, const indigo_post *posts, unsigned count,
                         unsigned focus)
{
    indigo_timeline *t = &app->thread;

    indigo_timeline_clear(t);
    for (unsigned i = 0; posts && i < count; i++) {
        if (!indigo_timeline_append(t, &posts[i])) {
            break;
        }
    }
    app->thread_focus = focus < t->count ? focus : 0;
    indigo_timeline_select(t, app->thread_focus, INDIGO_TIMELINE_ROWS);
    if (t->count == 0) {
        indigo_timeline_fail_fetch(t, "That post is not available.");
    }
}

void
indigo_app_thread_failed(indigo_app *app, const char *message)
{
    indigo_timeline_fail_fetch(&app->thread, message);
}

void
indigo_app_profile_loaded(indigo_app *app, const indigo_profile *p)
{
    app->profile = *p;
    app->profile.loading = false;
}

void
indigo_app_profile_failed(indigo_app *app, const char *message)
{
    app->profile.loading = false;
    indigo_copy_utf8(app->profile.status, sizeof app->profile.status, message);
    app->profile.status_is_error = true;
}

void
indigo_app_notifications_loaded(indigo_app *app, const indigo_notification *items,
                                unsigned count)
{
    indigo_notifications *n = &app->notifications;

    indigo_notifications_clear(n);
    for (unsigned i = 0; items && i < count && i < INDIGO_NOTIFICATION_MAX; i++) {
        n->items[n->count++] = items[i];
    }
    if (n->count == 0) {
        indigo_copy_utf8(n->status, sizeof n->status, "No notifications yet.");
    }
}

void
indigo_app_notifications_failed(indigo_app *app, const char *message)
{
    app->notifications.loading = false;
    indigo_copy_utf8(app->notifications.status, sizeof app->notifications.status, message);
    app->notifications.status_is_error = true;
}

void
indigo_app_set_query(indigo_app *app, const char *text)
{
    indigo_search *s = &app->search;

    indigo_copy_utf8(s->query, sizeof s->query, text ? text : "");
    /* A changed query makes the old results stale, and leaving them up would
     * invite opening a profile for someone the new query never matched. */
    s->count = 0;
    s->selected = 0;
    s->scroll = 0;
    s->searched = false;
    s->status[0] = '\0';
    s->status_is_error = false;
    indigo_app_submit_search(app);
}

void
indigo_app_search_loaded(indigo_app *app, const indigo_actor *actors, unsigned count,
                         const char *next_cursor)
{
    indigo_search *s = &app->search;

    for (unsigned i = 0; actors && i < count && s->count < INDIGO_SEARCH_MAX; i++) {
        s->results.actors[s->count++] = actors[i];
    }
    indigo_search_finish_page(s, count, next_cursor);
    if (s->count == 0) {
        indigo_copy_utf8(s->status, sizeof s->status, "Nobody matched that.");
    }
}

void
indigo_app_search_failed(indigo_app *app, const char *message)
{
    indigo_search_fail_page(&app->search, message);
}

void
indigo_app_open_people(indigo_app *app, indigo_search_kind kind, const char *subject)
{
    if (kind == INDIGO_SEARCH_PEOPLE || !subject || !subject[0]) {
        return;
    }
    app->screen = INDIGO_SCREEN_SEARCH;
    app->search.kind = kind;
    indigo_search_begin_page(&app->search, false);
    indigo_copy_utf8(app->search.subject, sizeof app->search.subject, subject);
    app->request_people = kind;
    indigo_copy_utf8(app->request_subject, sizeof app->request_subject, subject);
    app->request = INDIGO_REQUEST_PEOPLE;
}

static void begin_author_tab(indigo_app *app, const char *actor, int tab) {
  app->screen = INDIGO_SCREEN_SEARCH;
  app->search.kind = INDIGO_SEARCH_AUTHOR;
  indigo_search_begin_page(&app->search, false);
  app->search.tab = tab;
  indigo_copy_utf8(app->search.subject, sizeof app->search.subject, actor);
  indigo_copy_utf8(app->request_actor, sizeof app->request_actor, actor);
  app->request = INDIGO_REQUEST_AUTHOR_FEED;
}

void indigo_app_open_author_posts(indigo_app *app, const char *actor) {
  if (actor && actor[0]) {
    begin_author_tab(app, actor, WF_PROFILE_TAB_POSTS);
  }
}

/* The tab after the one on screen. The likes tab is only offered for the
 * signed-in account, so the cycle skips it for anyone else. */
int indigo_app_author_tab_after(const indigo_app *app) {
  bool self = strcasecmp(app->search.subject, app->signin.account) == 0;

  return wf_profile_tab_next(app->search.tab, self);
}

void indigo_app_next_author_tab(indigo_app *app) {
  char actor[INDIGO_POST_NAME_MAX];

  if (app->search.kind != INDIGO_SEARCH_AUTHOR || app->search.loading) {
    return;
  }
  indigo_copy_utf8(actor, sizeof actor, app->request_actor);
  begin_author_tab(app, actor, indigo_app_author_tab_after(app));
}

void
indigo_app_open_lists(indigo_app *app)
{
    indigo_app_push_screen(app);
    app->screen = INDIGO_SCREEN_SEARCH;
    app->search.kind = INDIGO_SEARCH_LISTS;
    /* A fresh fetch replaces the held lists too: they were the members'
     * Back target, and a new fetch makes them stale. */
    indigo_search_begin_page(&app->search, false);
    memset(app->search.held_lists, 0, sizeof app->search.held_lists);
    app->search.held_count = 0;
    app->request = INDIGO_REQUEST_LISTS;
}

void
indigo_app_open_feeds(indigo_app *app)
{
    indigo_app_push_screen(app);
    app->screen = INDIGO_SCREEN_SEARCH;
    app->search.kind = INDIGO_SEARCH_FEEDS;
    indigo_search_begin_page(&app->search, false);
    app->request = INDIGO_REQUEST_FEEDS;
}

void
indigo_app_open_mutes(indigo_app *app)
{
    indigo_app_push_screen(app);
    app->screen = INDIGO_SCREEN_SEARCH;
    app->search.kind = INDIGO_SEARCH_MUTED;
    indigo_search_begin_page(&app->search, false);
    app->request = INDIGO_REQUEST_MUTES;
}

void
indigo_app_open_blocks(indigo_app *app)
{
    indigo_app_push_screen(app);
    app->screen = INDIGO_SCREEN_SEARCH;
    app->search.kind = INDIGO_SEARCH_BLOCKED;
    indigo_search_begin_page(&app->search, false);
    app->request = INDIGO_REQUEST_BLOCKS;
}

void
indigo_app_open_feed(indigo_app *app, const char *feed_uri, const char *name)
{
    if (!feed_uri || !feed_uri[0]) {
        return;
    }
    /* The picker stays on the search screen; the feed shows on the home
     * screen, so Back lands on the picker again. */
    indigo_app_push_screen(app);
    indigo_copy_utf8(app->feed_uri, sizeof app->feed_uri, feed_uri);
    indigo_copy_utf8(app->feed_name, sizeof app->feed_name,
                     name && name[0] ? name : "Feed");
    indigo_copy_utf8(app->request_feed_uri, sizeof app->request_feed_uri, feed_uri);
    indigo_copy_utf8(app->request_feed_name, sizeof app->request_feed_name,
                     name && name[0] ? name : "Feed");
    app->screen = INDIGO_SCREEN_HOME;
    indigo_timeline_begin_fetch(&app->timeline, true);
    app->request = INDIGO_REQUEST_FEED;
}

void
indigo_app_feeds_loaded(indigo_app *app, const indigo_list *feeds, unsigned count,
                        const char *next_cursor)
{
    indigo_search *s = &app->search;

    if (s->kind != INDIGO_SEARCH_FEEDS) {
        return;
    }
    for (unsigned i = 0; i < count && s->count < INDIGO_SEARCH_MAX; i++) {
        s->results.lists[i] = feeds[i];
        s->count++;
    }
    indigo_search_finish_page(s, count, next_cursor);
    if (s->count == 0) {
        indigo_copy_utf8(s->status, sizeof s->status, "No saved feeds.");
        s->status_is_error = false;
    }
}

void
indigo_app_open_list_members(indigo_app *app, const char *list_uri, const char *name)
{
    indigo_search *s = &app->search;

    if (!list_uri || !list_uri[0]) {
        return;
    }
    /* Keep the lists that are being browsed: their members are actors and
     * land in the same union, so without this the members would overwrite
     * the lists and Back would return to a corrupted screen. */
    if (s->kind == INDIGO_SEARCH_LISTS) {
        memcpy(s->held_lists, s->results.lists, sizeof s->held_lists);
        s->held_count = s->count;
    }
    indigo_app_push_screen(app);
    app->screen = INDIGO_SCREEN_SEARCH;
    s->kind = INDIGO_SEARCH_LIST_MEMBERS;
    indigo_search_begin_page(s, false);
    indigo_copy_utf8(s->subject, sizeof s->subject, name && name[0] ? name : "Members");
    indigo_copy_utf8(app->request_list_uri, sizeof app->request_list_uri, list_uri);
    app->request = INDIGO_REQUEST_LIST_MEMBERS;
}

void
indigo_app_lists_loaded(indigo_app *app, const indigo_list *lists, unsigned count,
                        const char *next_cursor)
{
    indigo_search *s = &app->search;

    if (s->kind != INDIGO_SEARCH_LISTS) {
        return;
    }
    for (unsigned i = 0; i < count && s->count < INDIGO_SEARCH_MAX; i++) {
        s->results.lists[i] = lists[i];
        s->count++;
    }
    indigo_search_finish_page(s, count, next_cursor);
    if (s->count == 0) {
        indigo_copy_utf8(s->status, sizeof s->status, "No lists yet.");
        s->status_is_error = false;
    }
}

void
indigo_app_post_search_loaded(indigo_app *app, const indigo_post *posts, unsigned count,
                               const char *next_cursor)
{
    indigo_search *s = &app->search;

    for (unsigned i = 0; i < count && s->count < INDIGO_SEARCH_MAX; i++) {
        s->results.posts[s->count++] = posts[i];
    }
    indigo_search_finish_page(s, count, next_cursor);
    if (s->count == 0) {
        indigo_copy_utf8(s->status, sizeof s->status, "No posts matched that.");
    }
}

void
indigo_app_toggle_follow(indigo_app *app)
{
    indigo_profile *p = &app->profile;

    if (p->follow_busy || app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    /* Following needs the did, which only a loaded profile carries. Guarded
     * here rather than only at the call site, because main.c drives the
     * request from app state this function has to have set up. */
    if (!p->loaded || p->loading) {
        return;
    }
    /* An unfollow needs the record URI. Without it there is nothing to
     * delete, so the button stays out rather than failing after the press. */
    if (p->following && !p->follow_uri[0]) {
        indigo_copy_utf8(p->status, sizeof p->status, "Reload the profile first.");
        p->status_is_error = true;
        return;
    }
    p->following = !p->following;
    p->follow_busy = true;
    p->status[0] = '\0';
    p->status_is_error = false;
    app->request = INDIGO_REQUEST_FOLLOW;
    /* app->profile.following is already the intended state; the failure path
     * flips it back if the server disagrees. */
    app->request_follow = p->following;
}

void
indigo_app_follow_done(indigo_app *app, bool following, const char *follow_uri)
{
    indigo_profile *p = &app->profile;

    p->follow_busy = false;
    p->following = following;
    if (following && follow_uri && follow_uri[0]) {
        indigo_copy_utf8(p->follow_uri, sizeof p->follow_uri, follow_uri);
    } else {
        /* Unfollowed: the record is gone, so keeping the URI would make a
         * second unfollow try to delete something that no longer exists. */
        p->follow_uri[0] = '\0';
    }
}

void
indigo_app_follow_failed(indigo_app *app, const char *message)
{
    indigo_profile *p = &app->profile;

    p->follow_busy = false;
    p->following = !p->following;
    indigo_copy_utf8(p->status, sizeof p->status, message);
    p->status_is_error = true;
}

/* Mute and block share a shape: flip the flag optimistically, mark the
 * request, and put it back if the write fails. Blocking needs the record URI
 * to undo itself, so like unfollowing it refuses without one. */
void
indigo_app_toggle_mute(indigo_app *app)
{
    indigo_profile *p = &app->profile;

    if (!p->loaded || p->loading || p->mute_busy || app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    p->muted = !p->muted;
    p->mute_busy = true;
    app->request_graph = p->muted ? INDIGO_GRAPH_MUTE : INDIGO_GRAPH_UNMUTE;
    app->request = INDIGO_REQUEST_GRAPH;
}

void
indigo_app_toggle_block(indigo_app *app)
{
    indigo_profile *p = &app->profile;

    if (!p->loaded || p->loading || p->block_busy || app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    if (p->blocked) {
        /* Unblocking deletes a record by URI, so without one there is nothing
         * to delete and the press is refused rather than failing after it. */
        if (!p->block_uri[0]) {
            indigo_copy_utf8(p->status, sizeof p->status, "Reload the profile first.");
            p->status_is_error = true;
            return;
        }
        app->request_graph = INDIGO_GRAPH_UNBLOCK;
        p->blocked = false;
    } else {
        p->blocked = true;
        app->request_graph = INDIGO_GRAPH_BLOCK;
    }
    p->block_busy = true;
    app->request = INDIGO_REQUEST_GRAPH;
}

void
indigo_app_graph_done(indigo_app *app, indigo_graph_action action, const char *block_uri)
{
    indigo_profile *p = &app->profile;

    switch (action) {
    case INDIGO_GRAPH_MUTE:
        p->mute_busy = false;
        p->muted = true;
        break;
    case INDIGO_GRAPH_UNMUTE:
        p->mute_busy = false;
        p->muted = false;
        break;
    case INDIGO_GRAPH_BLOCK:
        p->block_busy = false;
        p->blocked = true;
        indigo_copy_utf8(p->block_uri, sizeof p->block_uri,
                         block_uri && block_uri[0] ? block_uri : "");
        break;
    case INDIGO_GRAPH_UNBLOCK:
        p->block_busy = false;
        p->blocked = false;
        p->block_uri[0] = '\0';
        break;
    case INDIGO_GRAPH_NONE:
        break;
    }
}

void
indigo_app_graph_failed(indigo_app *app, indigo_graph_action action, const char *message)
{
    indigo_profile *p = &app->profile;

    switch (action) {
    case INDIGO_GRAPH_MUTE:
        p->mute_busy = false;
        p->muted = false;
        break;
    case INDIGO_GRAPH_UNMUTE:
        p->mute_busy = false;
        p->muted = true;
        break;
    case INDIGO_GRAPH_BLOCK:
        p->block_busy = false;
        p->blocked = false;
        p->block_uri[0] = '\0';
        break;
    case INDIGO_GRAPH_UNBLOCK:
        p->block_busy = false;
        p->blocked = true;
        break;
    case INDIGO_GRAPH_NONE:
        break;
    }
    indigo_copy_utf8(p->status, sizeof p->status, message);
    p->status_is_error = true;
}

void
indigo_app_set_draft(indigo_app *app, const char *text)
{
    indigo_copy_utf8(app->compose.text, sizeof app->compose.text, text);
    app->compose.status[0] = '\0';
}

void
indigo_app_set_image_alt(indigo_app *app, const char *text)
{
    indigo_copy_utf8(app->compose.image_alt, sizeof app->compose.image_alt, text);
}

static void
publish_finish(indigo_app *app, const char *notice)
{
    indigo_compose *c = &app->compose;
    indigo_compose_mode mode = c->mode;

    c->sending = false;
    c->text[0] = '\0';
    memset(c->thread_texts, 0, sizeof c->thread_texts);
    c->thread_count = 0;
    indigo_compose_clear_image(c);
    c->status[0] = '\0';
    c->has_target = false;
    if (app->screen == INDIGO_SCREEN_COMPOSE) {
        indigo_app_go_back(app);
    }
    /* Show the person what they just published. */
    if (mode == INDIGO_COMPOSE_REPLY && app->thread_uri[0]) {
        indigo_app_load_thread(app, app->thread_uri);
    } else {
        indigo_app_refresh_timeline(app);
    }
    if (notice && notice[0]) {
        indigo_copy_utf8(app->timeline.status, sizeof app->timeline.status, notice);
        app->timeline.status_is_error = true;
    }
}

void
indigo_app_publish_done(indigo_app *app)
{
    publish_finish(app, NULL);
}

void
indigo_app_publish_partial(indigo_app *app, const char *message)
{
    publish_finish(app, message && message[0] ? message
                                             : "Only part of the thread was published.");
}

void
indigo_app_publish_failed(indigo_app *app, const char *message)
{
    indigo_compose *c = &app->compose;

    c->sending = false;
    indigo_copy_utf8(c->status, sizeof c->status, message);
    c->status_is_error = true;
}

void
indigo_app_delete_done(indigo_app *app, const char *post_uri)
{
    app->confirm_delete = false;
    app->delete_uri[0] = '\0';
    indigo_timeline_remove_post(&app->timeline, post_uri, INDIGO_TIMELINE_ROWS);
    /* Search and author-post results use the same bounded post array. */
    if (indigo_search_is_posts(&app->search)) {
        unsigned out = 0;
        for (unsigned i = 0; i < app->search.count; i++) {
            if (strcmp(app->search.results.posts[i].uri, post_uri) == 0) {
                continue;
            }
            if (out != i) {
                app->search.results.posts[out] = app->search.results.posts[i];
            }
            out++;
        }
        app->search.count = out;
        if (out == 0) {
            app->search.selected = 0;
            app->search.scroll = 0;
        } else {
            if (app->search.selected >= out) {
                app->search.selected = out - 1;
            }
            if (app->search.scroll >= out) {
                app->search.scroll = out - 1;
            }
        }
    }
    indigo_timeline_clear(&app->thread);
    if (app->screen == INDIGO_SCREEN_THREAD) {
        indigo_app_go_back(app);
    }
    indigo_copy_utf8(app->timeline.status, sizeof app->timeline.status, "Post deleted.");
    app->timeline.status_is_error = false;
    app->thread_uri[0] = '\0';
}

void
indigo_app_delete_failed(indigo_app *app, const char *message)
{
    app->confirm_delete = false;
    app->delete_uri[0] = '\0';
    if (app->screen == INDIGO_SCREEN_THREAD) {
        indigo_timeline_fail_fetch(&app->thread, message);
    } else {
        indigo_copy_utf8(app->timeline.status, sizeof app->timeline.status, message);
        app->timeline.status_is_error = true;
    }
}

void
indigo_app_set_updater(indigo_app *app, const char *describe, bool can_swap)
{
    indigo_updater_init(&app->updater, describe, can_swap);
}

void
indigo_app_open_update(indigo_app *app)
{
    indigo_app_push_screen(app);
    app->screen = INDIGO_SCREEN_UPDATE;
}

void
indigo_app_open_settings(indigo_app *app)
{
    indigo_app_push_screen(app);
    app->screen = INDIGO_SCREEN_SETTINGS;
    app->settings_selected = 0;
}
