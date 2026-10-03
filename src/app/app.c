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
    *app = (indigo_app) {
        .screen = INDIGO_SCREEN_SIGNIN,
        .quit_requested = false,
    };
    indigo_signin_init(&app->signin);
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
update_signed_in(indigo_app *app, const indigo_input *input)
{
    if (input->back) {
        app->screen = INDIGO_SCREEN_HOME;
    }
    if (input->confirm) {
        app->screen = INDIGO_SCREEN_PROFILE;
    }
    if (input->touch_pressed) {
        switch (indigo_layout_hit(app->screen, input->touch_x, input->touch_y)) {
        case INDIGO_ACTION_PROFILE:
            app->screen = INDIGO_SCREEN_PROFILE;
            break;
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
    } else {
        update_signed_in(app, input);
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
    set_status(s, message, false);
    app->screen = INDIGO_SCREEN_SIGNIN;
}
