#include "app/app.h"
#include "atproto/atproto.h"
#include "app/signin.h"
#include "atproto/session.h"
#include "input/input.h"
#include "input/textinput.h"
#include "ui/ui.h"
#include "util/log.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define DATA_DIR "sdmc:/3ds/indigo"
#define LOG_PATH DATA_DIR "/indigo.log"
#define SESSION_PATH DATA_DIR "/session.dat"
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

    if (!indigo_session_submit_publish(c->mode, c->text, c->has_target ? c->target.uri : "",
                                       c->has_target ? c->target.cid : "", c->root_uri,
                                       c->root_cid)) {
        indigo_app_publish_failed(app, "Could not start posting.");
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
    if (kind >= INDIGO_REQUEST_TIMELINE_REFRESH && indigo_session_busy()) {
        return;
    }
    switch (indigo_app_take_request(app, &f)) {
    case INDIGO_REQUEST_EDIT_FIELD:
        handle_edit(app, f);
        break;
    case INDIGO_REQUEST_SIGN_IN:
        if (indigo_session_submit_login(app->signin.service, app->signin.handle,
                                        app->signin.password)) {
            indigo_app_begin_sign_in(app, "Signing in...");
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
    case INDIGO_REQUEST_PUBLISH:
        start_publish(app);
        break;
    case INDIGO_REQUEST_EDIT_QUERY:
        handle_edit_query(app);
        break;
    case INDIGO_REQUEST_SEARCH:
        if (!indigo_session_submit_search(app->search.query)) {
            indigo_app_search_failed(app, "Could not start the request.");
        }
        break;
    case INDIGO_REQUEST_NONE:
        break;
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
            indigo_app_publish_done(app);
            break;
        case INDIGO_SESSION_EVENT_PUBLISH_FAILED:
            indigo_app_publish_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_SEARCH_PAGE: {
            unsigned n;
            const indigo_actor *actors = indigo_session_search_results(&n);

            indigo_app_search_loaded(app, actors, n);
            break;
        }
        case INDIGO_SESSION_EVENT_SEARCH_FAILED:
            indigo_app_search_failed(app, indigo_failure_message(ev.failure));
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
main(void)
{
    indigo_log_init();
    mkdir("sdmc:/3ds", 0777);
    mkdir(DATA_DIR, 0777);
    if (!indigo_log_open_file(LOG_PATH)) {
        indigo_log_warn("no log file; continuing with stderr only");
    }

    gfxInitDefault();
    romfsInit();

    if (!indigo_ui_init()) {
        indigo_log_error("could not initialise the 3DS renderer");
        romfsExit();
        gfxExit();
        indigo_log_shutdown();
        return 1;
    }

    indigo_atproto_init();
    indigo_session_start(SESSION_PATH);

    indigo_input input;
    indigo_input_init(&input);

    static indigo_app app; /* about 90KB: keep it off the stack */
    indigo_app_init(&app);
    app.wolfram_linked = indigo_atproto_available();

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
        indigo_ui_draw(&app, &input);

        gspWaitForVBlank();
    }

    indigo_app_shutdown(&app);
    indigo_session_stop();
    indigo_input_shutdown(&input);
    indigo_atproto_shutdown();
    indigo_ui_shutdown();
    romfsExit();
    gfxExit();
    indigo_log_shutdown();

    return 0;
}
