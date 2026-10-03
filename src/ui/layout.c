#include "ui/layout.h"

#include "app/signin.h"

#define COL_BG_TOP INDIGO_RGBA(18, 20, 26, 255)
#define COL_BG_BOTTOM INDIGO_RGBA(12, 14, 18, 255)
#define COL_BAR INDIGO_RGBA(30, 34, 44, 255)
#define COL_TEXT INDIGO_RGBA(255, 255, 255, 255)
#define COL_TEXT_SOFT INDIGO_RGBA(220, 224, 232, 255)
#define COL_TEXT_DIM INDIGO_RGBA(160, 168, 184, 255)
#define COL_PILL INDIGO_RGBA(44, 50, 66, 255)
#define COL_PILL_ACTIVE INDIGO_RGBA(74, 96, 180, 255)

/* A 20px gap keeps neighbouring pills clearly separate for stylus and finger. */
static const indigo_rect s_profile_button = {20, 124, 130, 44};
static const indigo_rect s_home_button = {170, 124, 130, 44};
static const indigo_rect s_sign_out_button = {20, 184, 280, 40};

/* Sign-in form: 8px between rows so a thumb never lands on two. */
static const indigo_rect s_field_service = {14, 52, 292, 38};
static const indigo_rect s_field_handle = {14, 98, 292, 38};
static const indigo_rect s_field_password = {14, 144, 292, 38};
static const indigo_rect s_sign_in_button = {14, 192, 292, 36};

indigo_rect
indigo_layout_button_rect(indigo_action action)
{
    switch (action) {
    case INDIGO_ACTION_PROFILE:
        return s_profile_button;
    case INDIGO_ACTION_HOME:
        return s_home_button;
    case INDIGO_ACTION_SIGN_OUT:
        return s_sign_out_button;
    case INDIGO_ACTION_FIELD_SERVICE:
        return s_field_service;
    case INDIGO_ACTION_FIELD_HANDLE:
        return s_field_handle;
    case INDIGO_ACTION_FIELD_PASSWORD:
        return s_field_password;
    case INDIGO_ACTION_SIGN_IN:
        return s_sign_in_button;
    case INDIGO_ACTION_NONE:
        break;
    }

    return (indigo_rect) {0, 0, 0, 0};
}

static bool
inside(indigo_rect r, int x, int y)
{
    return (float) x >= r.x && (float) x < r.x + r.w && (float) y >= r.y &&
           (float) y < r.y + r.h;
}

indigo_action
indigo_layout_hit(indigo_screen screen, int touch_x, int touch_y)
{
    static const indigo_action signin_actions[] = {
        INDIGO_ACTION_FIELD_SERVICE, INDIGO_ACTION_FIELD_HANDLE,
        INDIGO_ACTION_FIELD_PASSWORD, INDIGO_ACTION_SIGN_IN};
    static const indigo_action main_actions[] = {
        INDIGO_ACTION_PROFILE, INDIGO_ACTION_HOME, INDIGO_ACTION_SIGN_OUT};

    if (screen == INDIGO_SCREEN_SIGNIN) {
        for (unsigned i = 0; i < sizeof signin_actions / sizeof signin_actions[0]; i++) {
            if (inside(indigo_layout_button_rect(signin_actions[i]), touch_x, touch_y)) {
                return signin_actions[i];
            }
        }
        return INDIGO_ACTION_NONE;
    }
    for (unsigned i = 0; i < sizeof main_actions / sizeof main_actions[0]; i++) {
        if (inside(indigo_layout_button_rect(main_actions[i]), touch_x, touch_y)) {
            return main_actions[i];
        }
    }
    return INDIGO_ACTION_NONE;
}

#define COL_ERROR INDIGO_RGBA(255, 138, 128, 255)

static void
build_top_signin(const indigo_app *app, indigo_canvas *c)
{
    const indigo_signin *s = &app->signin;

    indigo_canvas_text(c, 18, 98, 0.8f, COL_TEXT, "Sign in");
    indigo_canvas_text(c, 18, 126, 0.6f, COL_TEXT_DIM,
                       "Use an app password, not your main password.");
    indigo_canvas_text(c, 18, 144, 0.6f, COL_TEXT_DIM,
                       "Make one in Settings, Privacy and security.");

    if (s->status[0]) {
        indigo_canvas_text(c, 18, 172, 0.65f,
                           s->status_is_error ? COL_ERROR : COL_TEXT_SOFT, "%s",
                           s->status);
    }
    indigo_canvas_text(c, 18, 208, 0.6f, COL_TEXT_DIM, "A  Sign in or edit   START  Exit");
}

static void
build_top(const indigo_app *app, indigo_canvas *c)
{
    indigo_canvas_init(c, INDIGO_TOP_WIDTH, INDIGO_TOP_HEIGHT);
    indigo_canvas_rect(c, 0, 0, INDIGO_TOP_WIDTH, INDIGO_TOP_HEIGHT, COL_BG_TOP);
    indigo_canvas_rect(c, 0, 0, INDIGO_TOP_WIDTH, 46, COL_BAR);
    indigo_canvas_text(c, 18, 10, 1.0f, COL_TEXT, "Indigo");

    indigo_canvas_text(c, 18, 62, 0.7f, COL_TEXT_SOFT,
                       "Native Bluesky client");

    if (app->screen == INDIGO_SCREEN_SIGNIN) {
        build_top_signin(app, c);
        return;
    }

    if (app->screen == INDIGO_SCREEN_HOME) {
        indigo_canvas_text(c, 18, 104, 0.8f, COL_TEXT, "Home");
        indigo_canvas_text(c, 18, 134, 0.65f, COL_TEXT_SOFT, "Signed in as %s",
                           app->signin.account);
        indigo_canvas_text(c, 18, 156, 0.6f, COL_TEXT_DIM,
                           "The timeline is not built yet.");
    } else {
        indigo_canvas_text(c, 18, 104, 0.8f, COL_TEXT, "Profile");
        indigo_canvas_text(c, 18, 134, 0.65f, COL_TEXT_DIM,
                           "Placeholder profile screen.");
    }

    indigo_canvas_text(c, 18, 180, 0.6f, COL_TEXT_DIM, "Wolfram: %s",
                       app->wolfram_linked ? "linked" : "not linked");
    indigo_canvas_text(c, 18, 208, 0.6f, COL_TEXT_DIM, "START  Exit");
}

static void
pill(indigo_canvas *c, indigo_action action, bool active, const char *label)
{
    indigo_rect r = indigo_layout_button_rect(action);

    indigo_canvas_rect(c, r.x, r.y, r.w, r.h, active ? COL_PILL_ACTIVE : COL_PILL);
    indigo_canvas_text(c, r.x + 14, r.y + 10, 0.7f, COL_TEXT, "%s", label);
}

static void
field_row(indigo_canvas *c, indigo_action action, const indigo_signin *s,
          indigo_field f)
{
    indigo_rect r = indigo_layout_button_rect(action);
    char shown[64];

    indigo_signin_display(s, f, shown, sizeof shown);
    indigo_canvas_rect(c, r.x, r.y, r.w, r.h, s->focus == f ? COL_PILL_ACTIVE : COL_PILL);
    indigo_canvas_text(c, r.x + 8, r.y + 2, 0.5f, COL_TEXT_SOFT, "%s",
                       indigo_signin_field_label(f));
    indigo_canvas_text(c, r.x + 8, r.y + 16, 0.65f, COL_TEXT, "%s",
                       shown[0] ? shown : "Tap to enter");
}

static void
build_bottom_signin(const indigo_app *app, indigo_canvas *c)
{
    const indigo_signin *s = &app->signin;
    indigo_rect b = indigo_layout_button_rect(INDIGO_ACTION_SIGN_IN);

    indigo_canvas_text(c, 14, 8, 0.75f, COL_TEXT, "Sign in");
    field_row(c, INDIGO_ACTION_FIELD_SERVICE, s, INDIGO_FIELD_SERVICE);
    field_row(c, INDIGO_ACTION_FIELD_HANDLE, s, INDIGO_FIELD_HANDLE);
    field_row(c, INDIGO_ACTION_FIELD_PASSWORD, s, INDIGO_FIELD_PASSWORD);

    indigo_canvas_rect(c, b.x, b.y, b.w, b.h,
                       indigo_signin_ready(s) && s->phase == INDIGO_PHASE_IDLE
                           ? COL_PILL_ACTIVE
                           : COL_PILL);
    indigo_canvas_text(c, b.x + 100, b.y + 8, 0.7f, COL_TEXT, "%s",
                       s->phase == INDIGO_PHASE_BUSY ? "Signing in..." : "Sign in");
}

static void
build_bottom(const indigo_app *app, const indigo_input *input, indigo_canvas *c)
{
    indigo_canvas_init(c, INDIGO_BOTTOM_WIDTH, INDIGO_BOTTOM_HEIGHT);
    indigo_canvas_rect(c, 0, 0, INDIGO_BOTTOM_WIDTH, INDIGO_BOTTOM_HEIGHT, COL_BG_BOTTOM);
    indigo_canvas_rect(c, 0, 0, INDIGO_BOTTOM_WIDTH, 42, COL_BAR);
    if (app->screen == INDIGO_SCREEN_SIGNIN) {
        build_bottom_signin(app, c);
        return;
    }
    indigo_canvas_text(c, 14, 8, 0.75f, COL_TEXT, "Touch input");

    indigo_canvas_text(c, 14, 52, 0.65f, COL_TEXT_SOFT, "x: %d  y: %d  %s",
                       input->touch_x, input->touch_y,
                       input->touch_down ? "touching" : "not touching");
    indigo_canvas_text(c, 14, 78, 0.6f, COL_TEXT_DIM, "Circle %d, %d   C-Stick %d, %d",
                       input->circle_x, input->circle_y, input->cstick_x, input->cstick_y);

    pill(c, INDIGO_ACTION_PROFILE, app->screen == INDIGO_SCREEN_PROFILE, "A  Profile");
    pill(c, INDIGO_ACTION_HOME, app->screen == INDIGO_SCREEN_HOME, "B  Home");
    pill(c, INDIGO_ACTION_SIGN_OUT, false, "Sign out");
}

void
indigo_layout_build(const indigo_app *app, const indigo_input *input,
                    indigo_canvas *top, indigo_canvas *bottom)
{
    build_top(app, top);
    build_bottom(app, input, bottom);
}
