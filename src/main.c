/*
 * Indigo -- Bluesky client for Nintendo 3DS.
 *
 * Copyright (c) 2024 Ewan Croft.
 */

#include "app/app.h"
#include "app/app_internal.h"
#include "app/credits.h"
#include "atproto/session.h"
#include "gfx/canvas.h"
#include "input/input.h"
#include "media/media.h"
#include "store/cache_store.h"
#include "store/draft_store.h"
#include "store/drafts_store.h"
#include "store/settings_store.h"
#include "ui/layout.h"
#include "ui/ui.h"
#include "util/buildinfo.h"
#include "util/log.h"

#include <3ds.h>
#include <citro2d.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define CACHE_PATH  "sdmc:/3ds/indigo/timeline.dat"
#define DRAFT_PATH  "sdmc:/3ds/indigo/draft.dat"
#define DRAFTS_PATH "sdmc:/3ds/indigo/drafts.dat"
#define QUEUE_PATH  "sdmc:/3ds/indigo/queue.dat"
#define SETTINGS_PATH "sdmc:/3ds/indigo/settings.dat"

static void
save_timeline_cache(indigo_app *app)
{
    indigo_store_status st;
    indigo_timeline_cache cache;
    unsigned n = 0;
    const indigo_post *posts = indigo_timeline_posts(&app->timeline, &n);

    if (!posts || n == 0) {
        return;
    }

    memset(&cache, 0, sizeof(cache));
    for (unsigned i = 0; i < n && i < INDIGO_TIMELINE_CACHE_MAX; i++) {
        if (indigo_timeline_cache_append(&cache, &posts[i])) {
            n = i + 1;
        } else {
            break;
        }
    }

    st = indigo_timeline_cache_save(&cache, CACHE_PATH);
    if (st != INDIGO_STORE_OK) {
        indigo_log_warn("could not save timeline cache (%d)", st);
    }
}

static void
load_timeline_cache(indigo_app *app)
{
    indigo_store_status st;
    indigo_timeline_cache cache;

    memset(&cache, 0, sizeof(cache));
    st = indigo_timeline_cache_load(&cache, CACHE_PATH);
    if (st == INDIGO_STORE_OK) {
        for (unsigned i = 0; i < cache.count; i++) {
            indigo_timeline_append(&app->timeline, &cache.posts[i]);
        }
        indigo_timeline_finish_fetch(&app->timeline, "");
    } else if (st != INDIGO_STORE_MISSING) {
        indigo_log_warn("could not load timeline cache (%d)", st);
    }
}

static void
load_draft(indigo_app *app)
{
    indigo_store_status st;
    indigo_draft draft;

    memset(&draft, 0, sizeof(draft));
    st = indigo_draft_load(&draft, DRAFT_PATH);
    if (st == INDIGO_STORE_OK) {
        indigo_app_set_draft(app, draft.text);
    } else if (st != INDIGO_STORE_MISSING) {
        indigo_log_warn("could not load draft (%d)", st);
    }
}

static void
load_queue(indigo_app *app)
{
    indigo_store_status st;
    indigo_post_queue queue;

    memset(&queue, 0, sizeof(queue));
    st = indigo_post_queue_load(&queue, QUEUE_PATH);
    if (st == INDIGO_STORE_OK) {
        for (unsigned i = 0; i < queue.count; i++) {
            indigo_app_queue_append(app, queue.items[i].text);
        }
    } else if (st != INDIGO_STORE_MISSING) {
        indigo_log_warn("could not load queue (%d)", st);
    }
}

static void
load_drafts(indigo_app *app)
{
    indigo_store_status st;
    indigo_drafts drafts;

    memset(&drafts, 0, sizeof(drafts));
    st = indigo_drafts_load(&drafts, DRAFTS_PATH);
    if (st == INDIGO_STORE_OK) {
        for (unsigned i = 0; i < drafts.count; i++) {
            indigo_app_drafts_append(app, drafts.records[i].text);
        }
    } else if (st != INDIGO_STORE_MISSING) {
        indigo_log_warn("could not load drafts (%d)", st);
    }
}

static void
handle_events(indigo_app *app)
{
    indigo_session_event ev;

    while (indigo_session_poll(&ev)) {
        switch (ev.kind) {
        case INDIGO_SESSION_EVENT_SIGNED_IN:
            indigo_app_sign_in_succeeded(app, ev.account);
            break;
        case INDIGO_SESSION_EVENT_SIGN_IN_FAILED:
            /* No message when there was simply nothing saved to resume. */
            indigo_app_sign_in_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_SIGNED_OUT:
            indigo_app_signed_out(app, "Signed out.");
            /* The decoded images and their GPU textures are not the
             * signed-out account's to keep, and on a shared device they are
             * a record of who was signed in before. The timeline cache is the
             * same kind of record, so it goes too. */
            indigo_ui_clear_images();
            if (indigo_file_remove(CACHE_PATH) == INDIGO_STORE_IO) {
                indigo_log_warn("could not remove the timeline cache");
            }
            break;
        case INDIGO_SESSION_EVENT_TIMELINE_PAGE: {
            unsigned n;
            const indigo_post *page = indigo_session_page(&n);
            bool first;

            for (unsigned i = 0; page && i < n; i++) {
                if (!indigo_timeline_append(&app->timeline, &page[i])) {
                    break;
                }
            }
            first = indigo_timeline_finish_fetch(&app->timeline, ev.cursor);
            /* Only the home timeline's first page is cached: a feed or a
             * search is not what the app opens on. */
            if (first && app->feed_uri[0] == '\0') {
                save_timeline_cache(app);
            }
            break;
        }
        case INDIGO_SESSION_EVENT_TIMELINE_FAILED:
            indigo_timeline_fail_fetch(&app->timeline, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_BOOKMARKS_PAGE: {
            unsigned n;
            const indigo_post *page = indigo_session_bookmarks(&n);

            /* Called even for an empty page, so a list with no bookmarks
             * finishes loading and says so instead of saying "Loading...". */
            if (ev.focus <= n) {
                indigo_app_bookmarks_loaded(app, page + ev.focus, n - ev.focus, ev.cursor);
            }
            break;
        }
        case INDIGO_SESSION_EVENT_BOOKMARKS_FAILED:
            indigo_app_bookmarks_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_BOOKMARK_DONE:
            indigo_app_bookmark_done(app, ev.post_uri, ev.bookmarked);
            break;
        case INDIGO_SESSION_EVENT_BOOKMARK_FAILED:
            indigo_app_bookmark_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_POST_ACTION_DONE:
            if (ev.action == INDIGO_POST_ACTION_LIKE || ev.action == INDIGO_POST_ACTION_UNLIKE) {
                indigo_app_set_like(app, ev.post_uri, ev.record_uri, false);
            } else {
                indigo_app_set_repost(app, ev.post_uri, ev.record_uri, false);
            }
            break;
        case INDIGO_SESSION_EVENT_POST_ACTION_FAILED: {
            const indigo_post *p;
            const char *prev = "";

            for (unsigned i = 0; i < app->timeline.count; i++) {
                p = &app->timeline.posts[i];
                if (strcmp(p->uri, ev.post_uri) == 0) {
                    prev = (ev.action == INDIGO_POST_ACTION_UNLIKE) ? p->like_uri
                           : (ev.action == INDIGO_POST_ACTION_UNREPOST) ? p->repost_uri
                                                                        : "";
                    break;
                }
            }
            action_undone(app, ev.action, ev.post_uri, prev);
            break;
        }
        case INDIGO_SESSION_EVENT_THREAD_PAGE: {
            unsigned n;
            const indigo_post *page = indigo_session_page(&n);

            indigo_app_thread_loaded(app, page, n, ev.focus, ev.thread_root);
            break;
        }
        case INDIGO_SESSION_EVENT_THREAD_FAILED:
            indigo_app_thread_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_PROFILE_LOADED:
            indigo_app_profile_loaded(app, indigo_session_profile());
            break;
        case INDIGO_SESSION_EVENT_PROFILE_FAILED:
            indigo_app_profile_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_PROFILE_UPDATE_DONE:
            indigo_app_profile_update_done(app);
            break;
        case INDIGO_SESSION_EVENT_PROFILE_UPDATE_FAILED:
            indigo_app_profile_update_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_NOTIFICATIONS_PAGE: {
            unsigned n;
            const indigo_notification *items = indigo_session_notifications(&n);

            indigo_app_notifications_loaded(app, items, n, ev.cursor);
            break;
        }
        case INDIGO_SESSION_EVENT_NOTIFICATIONS_FAILED:
            indigo_app_notifications_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_PUBLISHED:
            indigo_app_published(app, ev.compose_mode, ev.post_uri, ev.partial, ev.posted_count);
            break;
        case INDIGO_SESSION_EVENT_PUBLISH_FAILED:
            indigo_app_publish_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_SEARCH_PAGE: {
            unsigned n;
            const indigo_actor *items = indigo_session_search_results(&n);

            indigo_app_search_loaded(app, items, n, ev.cursor);
            break;
        }
        case INDIGO_SESSION_EVENT_SEARCH_FAILED:
            indigo_app_search_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_POST_SEARCH_PAGE: {
            unsigned n;
            const indigo_post *items;
            indigo_session_post_search_results(&items, &n);

            indigo_app_post_search_loaded(app, items, n, ev.cursor);
            break;
        }
        case INDIGO_SESSION_EVENT_LISTS_PAGE: {
            unsigned n;
            const indigo_list *items;
            indigo_session_lists(&items, &n);

            indigo_app_lists_loaded(app, items, n, ev.cursor);
            break;
        }
        case INDIGO_SESSION_EVENT_LIST_WRITE_DONE:
            indigo_app_list_write_done(app, "List updated");
            break;
        case INDIGO_SESSION_EVENT_LIST_WRITE_FAILED:
            indigo_app_list_write_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_FEEDS_PAGE: {
            unsigned n;
            const indigo_list *items;
            indigo_session_feeds(&items, &n);

            indigo_app_feeds_loaded(app, items, n, ev.cursor);
            break;
        }
        case INDIGO_SESSION_EVENT_STARTER_PACK_PAGE: {
            unsigned n;
            const indigo_starter_pack *pack;
            const indigo_actor *actors;
            unsigned count;
            indigo_session_starter_pack_results(&pack, &actors, &n);

            indigo_app_starter_pack_loaded(app, pack, actors, n, ev.cursor);
            break;
        }
        case INDIGO_SESSION_EVENT_STARTER_PACK_FAILED:
            indigo_app_starter_pack_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_FOLLOW_DONE:
            indigo_app_follow_done(app, true, ev.record_uri);
            break;
        case INDIGO_SESSION_EVENT_FOLLOW_FAILED:
            indigo_app_follow_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_GRAPH_DONE:
            indigo_app_graph_done(app, ev.graph, ev.record_uri);
            break;
        case INDIGO_SESSION_EVENT_GRAPH_FAILED:
            indigo_app_graph_failed(app, ev.graph, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_CHAT_PAGE: {
            unsigned n;
            const indigo_chat_inbox *inbox = indigo_session_chat_inbox();
            const indigo_chat_convo *items = inbox->items;
            n = inbox->count;

            indigo_app_chat_loaded(app, items, n, ev.cursor);
            break;
        }
        case INDIGO_SESSION_EVENT_CHAT_FAILED:
            indigo_app_chat_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_CHAT_THREAD_PAGE: {
            unsigned n;
            const indigo_chat_thread *thread = indigo_session_chat_thread();
            const indigo_chat_message *items = thread->items;
            n = thread->count;

            indigo_app_chat_thread_loaded(app, thread);
            break;
        }
        case INDIGO_SESSION_EVENT_CHAT_THREAD_FAILED:
            indigo_app_chat_thread_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_CHAT_SEND_DONE:
            indigo_app_chat_send_done(app);
            break;
        case INDIGO_SESSION_EVENT_CHAT_SEND_FAILED:
            indigo_app_chat_send_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_CHAT_REQUESTS_PAGE: {
            unsigned n;
            const indigo_chat_join_requests *requests = indigo_session_chat_requests();
            const indigo_chat_join_request *items = requests->items;
            n = requests->count;

            indigo_app_chat_requests_loaded(app, items, n, ev.cursor);
            break;
        }
        case INDIGO_SESSION_EVENT_CHAT_REQUESTS_FAILED:
            indigo_app_chat_requests_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_CHAT_ACCEPT_DONE:
            indigo_app_chat_accept_done(app);
            break;
        case INDIGO_SESSION_EVENT_CHAT_ACCEPT_FAILED:
            indigo_app_chat_accept_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_CHAT_DECLINE_DONE:
            indigo_app_chat_decline_done(app);
            break;
        case INDIGO_SESSION_EVENT_CHAT_DECLINE_FAILED:
            indigo_app_chat_decline_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_CHAT_START_DONE:
            const char *convo_id = indigo_session_chat_started();
            indigo_app_chat_start_done(app, convo_id);
            break;
        case INDIGO_SESSION_EVENT_CHAT_START_FAILED:
            indigo_app_chat_start_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_CHAT_JOIN_REQUESTS_PAGE: {
            unsigned n;
            const indigo_chat_join_requests *requests = indigo_session_chat_join_requests();
            const indigo_chat_join_request *items = requests->items;
            n = requests->count;

            indigo_app_chat_join_requests_loaded(app, items, n, ev.cursor);
            break;
        }
        case INDIGO_SESSION_EVENT_CHAT_JOIN_REQUESTS_FAILED:
            indigo_app_chat_join_requests_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_CHAT_JOIN_APPROVE_DONE:
            indigo_app_chat_join_request_done(app, true);
            break;
        case INDIGO_SESSION_EVENT_CHAT_JOIN_APPROVE_FAILED:
            indigo_app_chat_join_request_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_CHAT_JOIN_REJECT_DONE:
            indigo_app_chat_join_request_done(app, false);
            break;
        case INDIGO_SESSION_EVENT_CHAT_JOIN_REJECT_FAILED:
            indigo_app_chat_join_request_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_CHAT_GROUP_INFO_PAGE:
            indigo_app_chat_group_info_loaded(app, indigo_session_chat_group());
            break;
        case INDIGO_SESSION_EVENT_CHAT_GROUP_INFO_FAILED:
            indigo_app_chat_group_info_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_CHAT_GROUP_CREATE_DONE:
            indigo_app_chat_group_create_done(app, indigo_session_chat_group_started());
            break;
        case INDIGO_SESSION_EVENT_CHAT_GROUP_CREATE_FAILED:
            indigo_app_chat_group_create_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_CHAT_GROUP_RENAME_DONE:
            indigo_app_chat_group_rename_done(app);
            break;
        case INDIGO_SESSION_EVENT_CHAT_GROUP_RENAME_FAILED:
            indigo_app_chat_group_rename_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_CHAT_GROUP_ADD_DONE:
            indigo_app_chat_group_add_done(app);
            break;
        case INDIGO_SESSION_EVENT_CHAT_GROUP_ADD_FAILED:
            indigo_app_chat_group_add_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_CHAT_GROUP_REMOVE_DONE:
            indigo_app_chat_group_remove_done(app);
            break;
        case INDIGO_SESSION_EVENT_CHAT_GROUP_REMOVE_FAILED:
            indigo_app_chat_group_remove_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_CHAT_JOIN_LINK_DONE:
            indigo_app_chat_join_link_done(app);
            break;
        case INDIGO_SESSION_EVENT_CHAT_JOIN_LINK_FAILED:
            indigo_app_chat_join_link_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_FEED_DISCOVERY_PAGE:
            indigo_app_feed_discovery_loaded(app, indigo_session_feed_discovery_results());
            break;
        case INDIGO_SESSION_EVENT_FEED_DISCOVERY_FAILED:
            indigo_app_feed_discovery_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_FEED_GENERATOR_PAGE:
            indigo_app_feed_generator_loaded(app, indigo_session_feed_generator());
            break;
        case INDIGO_SESSION_EVENT_FEED_GENERATOR_FAILED:
            indigo_app_feed_generator_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_SAVED_FEEDS_V2_PAGE:
            indigo_app_saved_feeds_loaded(app, indigo_session_saved_feeds_v2());
            break;
        case INDIGO_SESSION_EVENT_SAVED_FEEDS_V2_FAILED:
            indigo_app_saved_feeds_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_SAVE_FEED_DONE:
            indigo_app_save_feed_done(app);
            break;
        case INDIGO_SESSION_EVENT_SAVE_FEED_FAILED:
            indigo_app_save_feed_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_REMOVE_FEED_DONE:
            indigo_app_remove_feed_done(app);
            break;
        case INDIGO_SESSION_EVENT_REMOVE_FEED_FAILED:
            indigo_app_remove_feed_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_PIN_FEED_DONE:
            indigo_app_pin_feed_done(app);
            break;
        case INDIGO_SESSION_EVENT_PIN_FEED_FAILED:
            indigo_app_pin_feed_failed(app, indigo_failure_message(ev.failure));
            break;
        }
    }
}



static void
handle_input(indigo_app *app, const indigo_input *input)
{
    if (input->pressed & KEY_START) {
        app->quit = true;
        return;
    }

    if (app->request) {
        indigo_app_handle_request_input(app, input);
        return;
    }

    indigo_app_handle_input(app, input);
}

static void
render(indigo_app *app)
{
    indigo_canvas canvas;

    indigo_canvas_init(&canvas, 400, 240);
    indigo_app_draw(app, &canvas);
    indigo_ui_render(&canvas);
    indigo_canvas_reset(&canvas);
}

static void
update(indigo_app *app)
{
    indigo_input input;
    indigo_app_update(app, &input);
}

static void
init_app(indigo_app *app)
{
    indigo_settings settings;
    indigo_store_status st;

    memset(app, 0, sizeof(*app));
    st = indigo_settings_load(&settings, SETTINGS_PATH);
    if (st == INDIGO_STORE_OK) {
        indigo_app_set_settings(app, &settings);
    } else if (st != INDIGO_STORE_MISSING) {
        indigo_log_warn("could not load settings (%d)", st);
    }

    indigo_app_init(app);
    load_timeline_cache(app);
    load_draft(app);
    load_queue(app);
    load_drafts(app);
}

static void
init_log(void)
{
    indigo_settings settings;
    indigo_store_status st;

    st = indigo_settings_load(&settings, SETTINGS_PATH);
    if (st == INDIGO_STORE_OK && !settings.diagnostics) {
        return;
    }

    indigo_log_init();
    indigo_log_info("Indigo %s (%s)", indigo_build_commit(), indigo_build_date());
}

static void
init_media(void)
{
    indigo_media_cache cache;
    indigo_media_init(&cache);
}

static void
init_session(void)
{
    indigo_session_start("sdmc:/3ds/indigo/session.dat");
}

static void
init_ui(void)
{
    indigo_ui_init();
}

static void
shutdown_media(void)
{
    indigo_media_shutdown();
}

static void
shutdown_session(void)
{
    indigo_session_stop();
}

static void
shutdown_ui(void)
{
    indigo_ui_shutdown();
}

static void
shutdown_log(void)
{
    indigo_log_shutdown();
}

int
main(void)
{
    indigo_app app;
    indigo_input input;

    gfxInitDefault();
    romfsInit();
    cfguInit();
    init_log();
    init_media();
    init_ui();
    init_session();
    init_app(&app);

    while (aptMainLoop()) {
        hidScanInput();
        indigo_input_begin_frame(&input);
        indigo_input_poll(&input);

        handle_input(&app, &input);
        if (app.quit) {
            break;
        }

        update(&app);
        handle_events(&app);
        render(&app);
        gspWaitForVBlank();
    }

    shutdown_app(&app);
    shutdown_session();
    shutdown_ui();
    shutdown_media();
    shutdown_log();
    cfguExit();
    romfsExit();
    gfxExit();
    return 0;
}