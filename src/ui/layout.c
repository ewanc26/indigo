#include "ui/layout.h"

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

indigo_rect
indigo_layout_button_rect(indigo_action action)
{
    switch (action) {
    case INDIGO_ACTION_PROFILE:
        return s_profile_button;
    case INDIGO_ACTION_HOME:
        return s_home_button;
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
indigo_layout_hit(int touch_x, int touch_y)
{
    if (inside(s_profile_button, touch_x, touch_y)) {
        return INDIGO_ACTION_PROFILE;
    }

    if (inside(s_home_button, touch_x, touch_y)) {
        return INDIGO_ACTION_HOME;
    }

    return INDIGO_ACTION_NONE;
}

static void
build_top(const indigo_app *app, indigo_canvas *c)
{
    indigo_canvas_init(c, INDIGO_TOP_WIDTH, INDIGO_TOP_HEIGHT);
    indigo_canvas_rect(c, 0, 0, INDIGO_TOP_WIDTH, INDIGO_TOP_HEIGHT, COL_BG_TOP);
    indigo_canvas_rect(c, 0, 0, INDIGO_TOP_WIDTH, 46, COL_BAR);
    indigo_canvas_text(c, 18, 10, 1.0f, COL_TEXT, "Indigo");

    indigo_canvas_text(c, 18, 62, 0.7f, COL_TEXT_SOFT,
                       "Native AT Protocol / Bluesky client");

    if (app->screen == INDIGO_SCREEN_HOME) {
        indigo_canvas_text(c, 18, 104, 0.8f, COL_TEXT, "Home");
        indigo_canvas_text(c, 18, 134, 0.65f, COL_TEXT_DIM,
                           "Sign-in and timeline are not built yet.");
    } else {
        indigo_canvas_text(c, 18, 104, 0.8f, COL_TEXT, "Profile");
        indigo_canvas_text(c, 18, 134, 0.65f, COL_TEXT_DIM,
                           "Placeholder profile screen.");
    }

    indigo_canvas_text(c, 18, 168, 0.65f, COL_TEXT_DIM, "Wolfram: %s",
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
build_bottom(const indigo_app *app, const indigo_input *input, indigo_canvas *c)
{
    indigo_canvas_init(c, INDIGO_BOTTOM_WIDTH, INDIGO_BOTTOM_HEIGHT);
    indigo_canvas_rect(c, 0, 0, INDIGO_BOTTOM_WIDTH, INDIGO_BOTTOM_HEIGHT, COL_BG_BOTTOM);
    indigo_canvas_rect(c, 0, 0, INDIGO_BOTTOM_WIDTH, 42, COL_BAR);
    indigo_canvas_text(c, 14, 8, 0.75f, COL_TEXT, "Touch input");

    indigo_canvas_text(c, 14, 52, 0.65f, COL_TEXT_SOFT, "x: %d  y: %d  %s",
                       input->touch_x, input->touch_y,
                       input->touch_down ? "touching" : "not touching");
    indigo_canvas_text(c, 14, 78, 0.6f, COL_TEXT_DIM, "Circle %d, %d   C-Stick %d, %d",
                       input->circle_x, input->circle_y, input->cstick_x, input->cstick_y);

    pill(c, INDIGO_ACTION_PROFILE, app->screen == INDIGO_SCREEN_PROFILE, "A  Profile");
    pill(c, INDIGO_ACTION_HOME, app->screen == INDIGO_SCREEN_HOME, "B  Home");
}

void
indigo_layout_build(const indigo_app *app, const indigo_input *input,
                    indigo_canvas *top, indigo_canvas *bottom)
{
    build_top(app, top);
    build_bottom(app, input, bottom);
}
