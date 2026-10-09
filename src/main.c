#include "app/app.h"
#include "atproto/atproto.h"
#include "app/signin.h"
#include "atproto/session.h"
#include "input/input.h"
#include "input/textinput.h"
#include "store/draft_store.h"
#include "store/settings_store.h"
#include "ui/ui.h"
#include "update/update_sd.h"
#include "update/update_worker.h"
#include "util/buildinfo.h"
#include "util/log.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define DATA_DIR "sdmc:/3ds/indigo"
#define LOG_PATH DATA_DIR "/indigo.log"
#define SESSION_PATH DATA_DIR "/session.dat"
#define SETTINGS_PATH DATA_DIR "/settings.dat"
#define DRAFT_PATH DATA_DIR "/draft.dat"
#define UPDATE_STATE_PATH DATA_DIR "/update.state"
#define AUTOFILL_PATH DATA_DIR "/autofill.txt"
#define COMPOSE_AUTOFILL_PATH DATA_DIR "/compose.txt"

static void
handle_edit(indigo_app *app, indigo_field f)
{
    char text[INDIGO_SERVICE_MAX];
    const char *current = f == INDIGO_FIELD_PASSWORD ? NULL : indigo_signin_field_value(&app->signin, f);
    indigo_text_result r;

    indigo_log_info("keyboard opened for field: %s", indigo_signin_field_label(f));
    r = indigo_text_edit(indigo_signin_field_label(f), current, f == INDIGO_FIELD_PASSWORD,
                         text, f == INDIGO_FIELD_PASSWORD ? INDIGO_PASSWORD_MAX : sizeof text);
    indigo_log_info("keyboard closed: %s", r == INDIGO_TEXT_OK ? "entered" : "cancelled");
    if (r == INDIGO_TEXT_OK) {
        indigo_app_set_field(app, f, text);
    }
    memset(text, 0, sizeof text);
}

/* Undo the optimistic "busy" mark when an action did not happen. */
static void
action_undone(indigo_app *app, indigo_post_action action, const char *post_uri,
              const char *undo_uri)
{
    switch (action) {
    case INDIGO_POST_ACTION_LIKE:
        indigo_app_set_like(app, post_uri, "", false);
        break;
    case INDIGO_POST_ACTION_UNLIKE:
        indigo_app_set_like(app, post_uri, undo_uri, false);
        break;
    case INDIGO_POST_ACTION_REPOST:
        indigo_app_set_repost(app, post_uri, "", false);
        break;
    case INDIGO_POST_ACTION_UNREPOST:
        indigo_app_set_repost(app, post_uri, undo_uri, false);
        break;
    case INDIGO_POST_ACTION_NONE:
        break;
    }
}

static void
post_action(indigo_app *app, indigo_post_action action)
{
    if (!indigo_session_submit_post_action(action, app->request_post_uri,
                                           app->request_post_cid, app->request_undo_uri)) {
        action_undone(app, action, app->request_post_uri, app->request_undo_uri);
    }
}

static void
handle_edit_draft(indigo_app *app)
{
    char text[INDIGO_DRAFT_MAX];
    indigo_text_result r;

#ifdef INDIGO_DEV_AUTOFILL
    /* Emulator aid only: the software keyboard cannot be driven there. */
    FILE *af = fopen(COMPOSE_AUTOFILL_PATH, "rb");

    if (af) {
        size_t n = fread(text, 1, sizeof text - 1, af);

        fclose(af);
        text[n] = '\0';
        while (n && (text[n - 1] == '\n' || text[n - 1] == '\r')) {
            text[--n] = '\0';
        }
        remove(COMPOSE_AUTOFILL_PATH);
        indigo_app_set_draft(app, text);
        return;
    }
#endif

    r = indigo_text_edit("What's on your mind?", app->compose.text, false, text, sizeof text);
    if (r == INDIGO_TEXT_OK) {
        indigo_app_set_draft(app, text);
    }
}

static void
start_publish(indigo_app *app)
{
    const indigo_compose *c = &app->compose;

    bool started;

    if (c->thread_count > 0) {
        const char *texts[INDIGO_THREAD_POSTS_MAX];
        int count = indigo_compose_thread_texts(c, texts);
        started = count >= 2 && indigo_session_submit_publish_thread(texts, (unsigned) count,
                                                                      (int) c->reply_gate);
    } else {
        started = indigo_session_submit_publish(c->mode, c->text,
                                                c->has_target ? c->target.uri : "",
                                                c->has_target ? c->target.cid : "", c->root_uri,
                                                c->root_cid, (int) c->reply_gate, c->image,
                                                c->image_alt);
    }
    if (!started) {
        indigo_app_publish_failed(app, "Could not start posting.");
    }
}

/* Alt text for the picture just chosen. Cancelling leaves it empty: alt text is
 * encouraged here, not required, and a post is not held up for it. */
static void
handle_edit_image_alt(indigo_app *app)
{
    char text[INDIGO_IMAGE_ALT_MAX];

    if (indigo_text_edit("Describe the picture", app->compose.image_alt, false, text,
                         sizeof text) == INDIGO_TEXT_OK) {
        indigo_app_set_image_alt(app, text);
    }
}

/* Accepting the keyboard runs the search: the prompt and the search are one
 * action, so there is no second confirm on a list screen. */
static void
handle_edit_query(indigo_app *app)
{
    char text[INDIGO_SEARCH_QUERY_MAX];
    indigo_text_result r = indigo_text_edit("Name or handle", app->search.query, false, text,
                                            sizeof text);

    if (r == INDIGO_TEXT_OK) {
        indigo_app_set_query(app, text);
    }
}

static void
handle_requests(indigo_app *app)
{
    indigo_field f;
    indigo_request_kind kind = indigo_app_peek_request(app);

    /* Network work waits its turn: one job at a time on the worker. */
    if (kind >= INDIGO_REQUEST_TIMELINE_REFRESH && kind != INDIGO_REQUEST_UPDATE_CHECK &&
        kind != INDIGO_REQUEST_UPDATE_INSTALL && indigo_session_busy()) {
        return;
    }
    switch (indigo_app_take_request(app, &f)) {
    case INDIGO_REQUEST_EDIT_FIELD:
        handle_edit(app, f);
        break;
    case INDIGO_REQUEST_SIGN_IN:
        if (app->signin.password[0]) {
            if (indigo_session_submit_login(app->signin.service, app->signin.handle,
                                            app->signin.password)) {
                indigo_app_begin_sign_in(app, "Signing in...");
            }
        } else {
            if (indigo_session_submit_oauth(app->signin.service, app->signin.handle)) {
                indigo_app_begin_sign_in(app, "Open the pairing link on another device...");
            }
        }
        break;
    case INDIGO_REQUEST_SIGN_OUT:
        if (indigo_session_submit_logout()) {
            indigo_app_begin_sign_in(app, "Signing out...");
        }
        break;
    case INDIGO_REQUEST_TIMELINE_REFRESH:
        if (!indigo_session_submit_timeline(NULL)) {
            indigo_timeline_fail_fetch(&app->timeline, "Could not start the request.");
        }
        break;
    case INDIGO_REQUEST_TIMELINE_MORE:
        if (!indigo_session_submit_timeline(app->timeline.cursor)) {
            indigo_timeline_fail_fetch(&app->timeline, "Could not start the request.");
        }
        break;
    case INDIGO_REQUEST_LIKE:
        post_action(app, INDIGO_POST_ACTION_LIKE);
        break;
    case INDIGO_REQUEST_UNLIKE:
        post_action(app, INDIGO_POST_ACTION_UNLIKE);
        break;
    case INDIGO_REQUEST_REPOST:
        post_action(app, INDIGO_POST_ACTION_REPOST);
        break;
    case INDIGO_REQUEST_UNREPOST:
        post_action(app, INDIGO_POST_ACTION_UNREPOST);
        break;
    case INDIGO_REQUEST_THREAD:
        if (!indigo_session_submit_thread(app->request_post_uri)) {
            indigo_app_thread_failed(app, "Could not start the request.");
        }
        break;
    case INDIGO_REQUEST_PROFILE:
        if (!indigo_session_submit_profile(app->request_post_uri)) {
            indigo_app_profile_failed(app, "Could not start the request.");
        }
        break;
    case INDIGO_REQUEST_NOTIFICATIONS:
        if (!indigo_session_submit_notifications()) {
            indigo_app_notifications_failed(app, "Could not start the request.");
        }
        break;
    case INDIGO_REQUEST_EDIT_DRAFT:
        handle_edit_draft(app);
        break;
    case INDIGO_REQUEST_EDIT_IMAGE_ALT:
        handle_edit_image_alt(app);
        break;
    case INDIGO_REQUEST_PUBLISH:
        start_publish(app);
        break;
    case INDIGO_REQUEST_DELETE_POST:
        if (!indigo_session_submit_delete_post(app->request_post_uri)) {
            indigo_app_delete_failed(app, "Could not start deleting that post.");
        }
        break;
    case INDIGO_REQUEST_EDIT_QUERY:
        handle_edit_query(app);
        break;
    case INDIGO_REQUEST_SEARCH:
        if (!indigo_session_submit_search(app->search.query, app->search.cursor[0] != '\0')) {
            indigo_app_search_failed(app, "Could not start the request.");
        }
        break;
    case INDIGO_REQUEST_FOLLOW:
        if (!indigo_session_submit_follow(app->request_follow ? INDIGO_FOLLOW : INDIGO_UNFOLLOW,
                                          app->profile.did, app->profile.follow_uri)) {
            indigo_app_follow_failed(app, "Could not start the request.");
        }
        break;
    case INDIGO_REQUEST_GRAPH:
        if (!indigo_session_submit_graph(app->request_graph, app->profile.did,
                                         app->profile.block_uri)) {
            indigo_app_graph_failed(app, app->request_graph, "Could not start the request.");
        }
        break;
    case INDIGO_REQUEST_PEOPLE:
        if (!indigo_session_submit_people(app->request_people, app->request_subject,
                                          app->search.cursor[0] != '\0')) {
            indigo_app_search_failed(app, "Could not start the request.");
        }
        break;
    case INDIGO_REQUEST_POST_SEARCH:
        if (!indigo_session_submit_post_search(app->search.query,
                                               app->search.cursor[0] != '\0')) {
            indigo_app_search_failed(app, "Could not start the request.");
        }
        break;
    case INDIGO_REQUEST_AUTHOR_FEED:
      if (!indigo_session_submit_author_feed(app->request_actor,
                                             app->search.tab,
                                             app->search.cursor[0] != '\0')) {
        indigo_app_search_failed(app, "Could not start the request.");
      }
        break;
    case INDIGO_REQUEST_LISTS:
        if (!indigo_session_submit_lists(app->search.cursor[0] != '\0')) {
            indigo_app_search_failed(app, "Could not start the request.");
        }
        break;
    case INDIGO_REQUEST_LIST_MEMBERS:
        if (!indigo_session_submit_list_members(app->request_list_uri,
                                                app->search.cursor[0] != '\0')) {
            indigo_app_search_failed(app, "Could not start the request.");
        }
        break;
    case INDIGO_REQUEST_FEEDS:
        if (!indigo_session_submit_feeds()) {
            indigo_app_search_failed(app, "Could not start the request.");
        }
        break;
    case INDIGO_REQUEST_MUTES:
        if (!indigo_session_submit_mutes(app->search.cursor[0] != '\0')) {
            indigo_app_search_failed(app, "Could not start the request.");
        }
        break;
    case INDIGO_REQUEST_BLOCKS:
        if (!indigo_session_submit_blocks(app->search.cursor[0] != '\0')) {
            indigo_app_search_failed(app, "Could not start the request.");
        }
        break;
    case INDIGO_REQUEST_FEED:
        if (!indigo_session_submit_feed(app->request_feed_uri,
                                         app->timeline.cursor[0]
                                             ? app->timeline.cursor : NULL)) {
            indigo_timeline_fail_fetch(&app->timeline, "Could not start the request.");
        }
        break;
    case INDIGO_REQUEST_SAVE_SETTINGS:
        indigo_settings_store_save(SETTINGS_PATH, &app->settings);
        break;
    case INDIGO_REQUEST_UPDATE_CHECK:
        if (!indigo_update_worker_check()) {
            indigo_updater_fail(&app->updater, "Could not start the check.");
        }
        break;
    case INDIGO_REQUEST_UPDATE_INSTALL:
        if (!indigo_update_worker_download()) {
            indigo_updater_fail(&app->updater, "Could not start the download.");
        }
        break;
    case INDIGO_REQUEST_NONE:
        break;
    }
}

/* Update results arrive on the main thread, and so does the swap: it needs
 * RomFS closed, which a worker must not do. After it, the file the app was
 * launched from is the backup, so the screen says to restart and nothing else
 * touches the card. */
static bool s_romfs_open = true;

static void
handle_update(indigo_app *app)
{
    indigo_update_event ev;

    if (app->updater.state == INDIGO_UPDATER_READY) {
        /* The frame that drew "putting it in place" came between the event and
         * this, because the event is polled after this check. */
        romfsExit();
        s_romfs_open = false;
        if (indigo_update_worker_install()) {
            indigo_updater_installed(&app->updater);
            indigo_log_info("update: %s is in place", app->updater.latest);
        } else {
            indigo_updater_fail(&app->updater,
                                "Could not put the new build in place. The old one is untouched or "
                                "restored the next time Indigo starts.");
        }
        /* RomFS stays closed either way: the trust store is gone, so the
         * screen does no more network work this run. */
    }
    while (indigo_update_worker_poll(&ev)) {
        switch (ev.kind) {
        case INDIGO_UPDATE_EVENT_CHECKED:
            indigo_updater_check_done(&app->updater, ev.version, ev.size, ev.is_update);
            break;
        case INDIGO_UPDATE_EVENT_STAGED:
            indigo_updater_staged(&app->updater);
            break;
        case INDIGO_UPDATE_EVENT_FAILED:
            indigo_updater_fail(&app->updater, ev.message);
            break;
        case INDIGO_UPDATE_EVENT_NONE:
            break;
        }
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
             * a record of who was signed in before. */
            indigo_ui_clear_images();
            break;
        case INDIGO_SESSION_EVENT_TIMELINE_PAGE: {
            unsigned n;
            const indigo_post *page = indigo_session_page(&n);

            for (unsigned i = 0; page && i < n; i++) {
                if (!indigo_timeline_append(&app->timeline, &page[i])) {
                    break;
                }
            }
            indigo_timeline_finish_fetch(&app->timeline, ev.cursor);
            break;
        }
        case INDIGO_SESSION_EVENT_TIMELINE_FAILED:
            indigo_timeline_fail_fetch(&app->timeline, indigo_failure_message(ev.failure));
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

            indigo_app_thread_loaded(app, page, n, ev.focus);
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
        case INDIGO_SESSION_EVENT_NOTIFICATIONS_PAGE: {
            unsigned n;
            const indigo_notification *items = indigo_session_notifications(&n);

            indigo_app_notifications_loaded(app, items, n);
            break;
        }
        case INDIGO_SESSION_EVENT_NOTIFICATIONS_FAILED:
            indigo_app_notifications_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_PUBLISHED:
            if (ev.partial) {
                indigo_app_publish_partial(app, ev.message);
            } else {
                indigo_app_publish_done(app);
            }
            break;
        case INDIGO_SESSION_EVENT_PUBLISH_FAILED:
            indigo_app_publish_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_POST_DELETED:
            indigo_app_delete_done(app, ev.post_uri);
            break;
        case INDIGO_SESSION_EVENT_POST_DELETE_FAILED:
            indigo_app_delete_failed(app, "Could not delete that post. It may still be there.");
            break;
        case INDIGO_SESSION_EVENT_SEARCH_PAGE: {
            unsigned n;
            const indigo_actor *actors = indigo_session_search_results(&n);

            indigo_app_search_loaded(app, actors, n, ev.cursor);
            break;
        }
        case INDIGO_SESSION_EVENT_SEARCH_FAILED:
            indigo_app_search_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_POST_SEARCH_PAGE: {
            const indigo_post *posts;
            unsigned pn;

            indigo_session_post_search_results(&posts, &pn);
            indigo_app_post_search_loaded(app, posts, pn, ev.cursor);
            break;
        }
        case INDIGO_SESSION_EVENT_LISTS_PAGE: {
            const indigo_list *lists;
            unsigned ln;

            indigo_session_lists_results(&lists, &ln);
            indigo_app_lists_loaded(app, lists, ln, ev.cursor);
            break;
        }
        case INDIGO_SESSION_EVENT_FEEDS_PAGE: {
            const indigo_list *feeds;
            unsigned fn;

            indigo_session_feeds_results(&feeds, &fn);
            indigo_app_feeds_loaded(app, feeds, fn, ev.cursor);
            break;
        }
        case INDIGO_SESSION_EVENT_FOLLOW_DONE:
            indigo_app_follow_done(app, ev.follow == INDIGO_FOLLOW, ev.record_uri);
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
        case INDIGO_SESSION_EVENT_NONE:
            break;
        }
    }
}

#ifdef INDIGO_DEV_AUTOFILL
/* Emulator aid only: never compiled into a normal build. */
static void
dev_autofill(indigo_app *app)
{
    char buf[1024];
    FILE *f = fopen(AUTOFILL_PATH, "rb");
    size_t n;

    if (!f) {
        return;
    }
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    if (indigo_signin_apply_autofill(&app->signin, buf) > 0) {
        indigo_log_warn("dev autofill applied (INDIGO_DEV_AUTOFILL build)");
        indigo_app_submit(app);
    }
    memset(buf, 0, sizeof buf);
}
#endif

int
main(int argc, char **argv)
{
    indigo_log_init();
    mkdir("sdmc:/3ds", 0777);
    mkdir(DATA_DIR, 0777);

    /* Before the log file is opened, because the diagnostics setting decides
     * whether one is. An absent file is the normal first-run case and leaves
     * the defaults in place, so it needs no branch of its own. */
    static indigo_settings settings;

    if (indigo_settings_store_load(SETTINGS_PATH, &settings) ==
        INDIGO_STORE_UNREADABLE) {
        indigo_log_warn("settings file was unreadable; continuing with defaults");
    }
    if (settings.diagnostics && !indigo_log_open_file(LOG_PATH)) {
        indigo_log_warn("no log file; continuing with stderr only");
    }
    if (settings.diagnostics) {
        indigo_log_info("wolfram v0.39.0 (linked)");
    }

    /* Before RomFS is mounted: recovery may have to move the .3dsx that RomFS
     * reads from. argv[0] is the path the Homebrew Menu launched. */
    indigo_update_sd_recover(argc > 0 ? argv[0] : NULL, UPDATE_STATE_PATH);

    gfxInitDefault();
    romfsInit();

    /* Before the renderer: the media loader owns a Wolfram client, and
     * Wolfram's 3DS platform layer initialises the socket service. */
    indigo_atproto_init();

    if (!indigo_ui_init()) {
        indigo_log_error("could not initialise the 3DS renderer");
        indigo_atproto_shutdown();
        romfsExit();
        gfxExit();
        indigo_log_shutdown();
        return 1;
    }

    indigo_session_start(SESSION_PATH);

    indigo_input input;
    indigo_input_init(&input);

    static indigo_app app; /* about 370KB, mostly posts: keep it off the stack */
    indigo_app_init(&app);
    indigo_app_set_settings(&app, &settings);
    {
        indigo_update_paths probe;
        bool can_swap = argc > 0 && indigo_update_paths_from(argv[0], UPDATE_STATE_PATH, &probe);

        indigo_update_worker_init(argc > 0 ? argv[0] : NULL, UPDATE_STATE_PATH);
        indigo_app_set_updater(&app, INDIGO_BUILD_COMMIT, can_swap);
    }
    app.wolfram_linked = indigo_atproto_available();

    /* An unsent post survives closing the app. The text is restored into the
     * compose buffer, so the existing "Draft kept from earlier." path tells
     * the person it is there; nothing is ever sent without their pressing it. */
    static char saved_draft[INDIGO_DRAFT_MAX];

    if (indigo_draft_store_load(DRAFT_PATH, saved_draft, sizeof saved_draft) ==
        INDIGO_STORE_UNREADABLE) {
        indigo_log_warn("draft file was unreadable; it was kept as draft.dat.bad");
    }
    indigo_app_set_draft(&app, saved_draft);

    if (indigo_session_has_saved() && indigo_session_submit_resume()) {
        indigo_app_begin_sign_in(&app, "Resuming your session...");
    }
#ifdef INDIGO_DEV_AUTOFILL
    else {
        dev_autofill(&app);
    }
#endif

    while (aptMainLoop() && !indigo_app_should_quit(&app)) {
        hidScanInput();

        indigo_input_begin_frame(&input);
        indigo_input_poll(&input);

        indigo_app_update(&app, &input);
        handle_requests(&app);
        handle_events(&app);
        handle_update(&app);
        /* Only on a change, so an idle compose screen costs no card writes.
         * The copy is updated even when the write fails: retrying every frame
         * would stall the loop on a full or removed card. */
        if (strcmp(saved_draft, app.compose.text) != 0) {
            indigo_copy_utf8(saved_draft, sizeof saved_draft, app.compose.text);
            if (indigo_draft_store_save(DRAFT_PATH, saved_draft) == INDIGO_STORE_IO) {
                indigo_log_warn("could not save the draft");
            }
        }
        indigo_ui_draw(&app, &input);

        gspWaitForVBlank();
    }

    indigo_app_shutdown(&app);
    indigo_update_worker_stop();
    indigo_session_stop();
    indigo_input_shutdown(&input);
    indigo_atproto_shutdown();
    indigo_ui_shutdown();
    if (s_romfs_open) {
        romfsExit();
    }
    gfxExit();
    indigo_log_shutdown();

    return 0;
}
