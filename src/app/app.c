#include "app/app.h"
#include "app/signin.h"
#include "input/input.h"
#include "ui/layout.h"

#include <string.h>

static void
set_status(indigo_signin *s, const char *msg, bool is_error)
{
    strncpy(s->status, msg ? msg : "", sizeof s->status - 1);
    s->status[sizeof s->status - 1] = '\0';
    s->status_is_error = is_error;
}

void
indigo_app_init(indigo_app *app)
{
    /* Large (the timeline is inline): zero in place, never via a temporary. */
    memset(app, 0, sizeof *app);
    app->screen = INDIGO_SCREEN_SIGNIN;
    indigo_signin_init(&app->signin);
    indigo_timeline_init(&app->timeline);
}

static void
request_edit(indigo_app *app, indigo_field f)
{
    app->signin.focus = f;
    app->request = INDIGO_REQUEST_EDIT_FIELD;
    app->request_field = f;
}

static void
try_sign_in(indigo_app *app)
{
    indigo_signin *s = &app->signin;

    if (s->phase == INDIGO_PHASE_BUSY) {
        return;
    }
    if (!indigo_signin_ready(s)) {
        set_status(s, "Fill in the service, handle and app password first.", true);
        return;
    }
    app->request = INDIGO_REQUEST_SIGN_IN;
}

bool
indigo_app_set_field(indigo_app *app, indigo_field f, const char *text)
{
    indigo_input_status st = indigo_signin_set_field(&app->signin, f, text);

    if (st != INDIGO_INPUT_OK) {
        set_status(&app->signin, indigo_input_status_message(st), true);
        return false;
    }
    set_status(&app->signin, "", false);
    return true;
}

void
indigo_app_submit(indigo_app *app)
{
    try_sign_in(app);
}

static void
update_signin(indigo_app *app, const indigo_input *input)
{
    indigo_signin *s = &app->signin;

    if (s->phase == INDIGO_PHASE_BUSY) {
        return;
    }
    if (input->confirm) {
        if (indigo_signin_ready(s)) {
            try_sign_in(app);
        } else {
            request_edit(app, s->focus);
        }
    }
    if (input->touch_pressed) {
        switch (indigo_layout_hit(app->screen, input->touch_x, input->touch_y)) {
        case INDIGO_ACTION_FIELD_SERVICE:
            request_edit(app, INDIGO_FIELD_SERVICE);
            break;
        case INDIGO_ACTION_FIELD_HANDLE:
            request_edit(app, INDIGO_FIELD_HANDLE);
            break;
        case INDIGO_ACTION_FIELD_PASSWORD:
            request_edit(app, INDIGO_FIELD_PASSWORD);
            break;
        case INDIGO_ACTION_SIGN_IN:
            try_sign_in(app);
            break;
        default:
            break;
        }
    }
}

static void
request_post_action(indigo_app *app, indigo_request_kind kind, const indigo_post *post,
                    const char *undo_uri)
{
    indigo_copy_utf8(app->request_post_uri, sizeof app->request_post_uri, post->uri);
    indigo_copy_utf8(app->request_post_cid, sizeof app->request_post_cid, post->cid);
    indigo_copy_utf8(app->request_undo_uri, sizeof app->request_undo_uri, undo_uri);
    app->request = kind;
}

static void
toggle_like(indigo_app *app)
{
    const indigo_post *p = indigo_timeline_selected(&app->timeline);

    if (!p || p->like_pending || app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    request_post_action(app, p->like_uri[0] ? INDIGO_REQUEST_UNLIKE : INDIGO_REQUEST_LIKE, p,
                        p->like_uri);
    indigo_timeline_set_like(&app->timeline, p->uri, p->like_uri, true);
}

static void
toggle_repost(indigo_app *app)
{
    const indigo_post *p = indigo_timeline_selected(&app->timeline);

    if (!p || p->repost_pending || app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    request_post_action(app, p->repost_uri[0] ? INDIGO_REQUEST_UNREPOST : INDIGO_REQUEST_REPOST,
                        p, p->repost_uri);
    indigo_timeline_set_repost(&app->timeline, p->uri, p->repost_uri, true);
}

static void
refresh_timeline(indigo_app *app)
{
    if (app->timeline.loading || app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    indigo_timeline_begin_fetch(&app->timeline, true);
    app->request = INDIGO_REQUEST_TIMELINE_REFRESH;
}

static void
update_home(indigo_app *app, const indigo_input *input)
{
    indigo_timeline *t = &app->timeline;

    if (input->confirm) {
        app->screen = INDIGO_SCREEN_PROFILE;
    }
    if (input->up) {
        indigo_timeline_move(t, -1, INDIGO_TIMELINE_ROWS);
    }
    if (input->down) {
        indigo_timeline_move(t, 1, INDIGO_TIMELINE_ROWS);
    }
    if (input->page_up) {
        indigo_timeline_move(t, -INDIGO_TIMELINE_ROWS, INDIGO_TIMELINE_ROWS);
    }
    if (input->page_down) {
        indigo_timeline_move(t, INDIGO_TIMELINE_ROWS, INDIGO_TIMELINE_ROWS);
    }
    if (input->like) {
        toggle_like(app);
    }
    if (input->repost) {
        toggle_repost(app);
    }
    if (input->refresh) {
        refresh_timeline(app);
    }
    if (input->touch_pressed) {
        indigo_action a = indigo_layout_hit(app->screen, input->touch_x, input->touch_y);

        switch (a) {
        case INDIGO_ACTION_ROW0:
        case INDIGO_ACTION_ROW1:
        case INDIGO_ACTION_ROW2:
            indigo_timeline_select(t, t->scroll + (unsigned) (a - INDIGO_ACTION_ROW0),
                                   INDIGO_TIMELINE_ROWS);
            break;
        case INDIGO_ACTION_LIKE:
            toggle_like(app);
            break;
        case INDIGO_ACTION_REPOST:
            toggle_repost(app);
            break;
        case INDIGO_ACTION_REFRESH:
            refresh_timeline(app);
            break;
        default:
            break;
        }
    }
    if (app->request == INDIGO_REQUEST_NONE && indigo_timeline_wants_page(t)) {
        indigo_timeline_begin_fetch(t, false);
        app->request = INDIGO_REQUEST_TIMELINE_MORE;
    }
}

static void
update_profile(indigo_app *app, const indigo_input *input)
{
    if (input->back) {
        app->screen = INDIGO_SCREEN_HOME;
    }
    if (input->touch_pressed) {
        switch (indigo_layout_hit(app->screen, input->touch_x, input->touch_y)) {
        case INDIGO_ACTION_HOME:
            app->screen = INDIGO_SCREEN_HOME;
            break;
        case INDIGO_ACTION_SIGN_OUT:
            app->request = INDIGO_REQUEST_SIGN_OUT;
            break;
        default:
            break;
        }
    }
}

void
indigo_app_update(indigo_app *app, const indigo_input *input)
{
    if (input->quit) {
        app->quit_requested = true;
    }

    if (app->screen == INDIGO_SCREEN_SIGNIN) {
        update_signin(app, input);
    } else if (app->screen == INDIGO_SCREEN_HOME) {
        update_home(app, input);
    } else {
        update_profile(app, input);
    }
}

void
indigo_app_shutdown(indigo_app *app)
{
    memset(app->signin.password, 0, sizeof app->signin.password);
}

bool
indigo_app_should_quit(const indigo_app *app)
{
    return app->quit_requested;
}

indigo_request_kind
indigo_app_peek_request(const indigo_app *app)
{
    return app->request;
}

indigo_request_kind
indigo_app_take_request(indigo_app *app, indigo_field *field)
{
    indigo_request_kind k = app->request;

    if (field) {
        *field = app->request_field;
    }
    app->request = INDIGO_REQUEST_NONE;
    return k;
}

void
indigo_app_begin_sign_in(indigo_app *app, const char *status)
{
    app->signin.phase = INDIGO_PHASE_BUSY;
    set_status(&app->signin, status, false);
}

void
indigo_app_sign_in_succeeded(indigo_app *app, const char *account)
{
    indigo_signin *s = &app->signin;

    s->phase = INDIGO_PHASE_IDLE;
    memset(s->password, 0, sizeof s->password);
    strncpy(s->account, account ? account : "", sizeof s->account - 1);
    s->account[sizeof s->account - 1] = '\0';
    set_status(s, "", false);
    app->screen = INDIGO_SCREEN_HOME;
    indigo_timeline_begin_fetch(&app->timeline, true);
    app->request = INDIGO_REQUEST_TIMELINE_REFRESH;
}

void
indigo_app_sign_in_failed(indigo_app *app, const char *message)
{
    app->signin.phase = INDIGO_PHASE_IDLE;
    set_status(&app->signin, message, true);
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
    set_status(s, message, false);
    app->screen = INDIGO_SCREEN_SIGNIN;
}
