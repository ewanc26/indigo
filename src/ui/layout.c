#include "ui/layout.h"

#include "app/signin.h"
#include "ui/wrap.h"

#define COL_BG_TOP INDIGO_RGBA(18, 20, 26, 255)
#define COL_BG_BOTTOM INDIGO_RGBA(12, 14, 18, 255)
#define COL_BAR INDIGO_RGBA(30, 34, 44, 255)
#define COL_TEXT INDIGO_RGBA(255, 255, 255, 255)
#define COL_TEXT_SOFT INDIGO_RGBA(220, 224, 232, 255)
#define COL_TEXT_DIM INDIGO_RGBA(160, 168, 184, 255)
#define COL_PILL INDIGO_RGBA(44, 50, 66, 255)
#define COL_PILL_ACTIVE INDIGO_RGBA(74, 96, 180, 255)

/* A 20px gap keeps neighbouring pills clearly separate for stylus and finger. */
static const indigo_rect s_home_button = {20, 124, 280, 44};
static const indigo_rect s_sign_out_button = {20, 184, 280, 40};

/* Timeline list: three rows with 4px between, then 12px-spaced action pills. */
#define ROW_X 8
#define ROW_W 304
#define ROW_H 46
#define ROW_STEP 50
#define ROW_Y0 48
static const indigo_rect s_like_button = {14, 202, 92, 34};
static const indigo_rect s_repost_button = {118, 202, 92, 34};
static const indigo_rect s_refresh_button = {222, 202, 92, 34};

/* Sign-in form: 8px between rows so a thumb never lands on two. */
static const indigo_rect s_field_service = {14, 52, 292, 38};
static const indigo_rect s_field_handle = {14, 98, 292, 38};
static const indigo_rect s_field_password = {14, 144, 292, 38};
static const indigo_rect s_sign_in_button = {14, 192, 292, 36};

indigo_rect
indigo_layout_button_rect(indigo_action action)
{
    switch (action) {
    case INDIGO_ACTION_ROW0:
    case INDIGO_ACTION_ROW1:
    case INDIGO_ACTION_ROW2:
        return (indigo_rect) {ROW_X, ROW_Y0 + ROW_STEP * (float) (action - INDIGO_ACTION_ROW0),
                              ROW_W, ROW_H};
    case INDIGO_ACTION_LIKE:
        return s_like_button;
    case INDIGO_ACTION_REPOST:
        return s_repost_button;
    case INDIGO_ACTION_REFRESH:
        return s_refresh_button;
    case INDIGO_ACTION_PROFILE:
        break;
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
    static const indigo_action home_actions[] = {
        INDIGO_ACTION_ROW0, INDIGO_ACTION_ROW1, INDIGO_ACTION_ROW2,
        INDIGO_ACTION_LIKE, INDIGO_ACTION_REPOST, INDIGO_ACTION_REFRESH};
    static const indigo_action profile_actions[] = {INDIGO_ACTION_HOME, INDIGO_ACTION_SIGN_OUT};
    const indigo_action *main_actions = screen == INDIGO_SCREEN_HOME ? home_actions
                                                                      : profile_actions;
    unsigned main_count = screen == INDIGO_SCREEN_HOME
                              ? sizeof home_actions / sizeof home_actions[0]
                              : sizeof profile_actions / sizeof profile_actions[0];

    if (screen == INDIGO_SCREEN_SIGNIN) {
        for (unsigned i = 0; i < sizeof signin_actions / sizeof signin_actions[0]; i++) {
            if (inside(indigo_layout_button_rect(signin_actions[i]), touch_x, touch_y)) {
                return signin_actions[i];
            }
        }
        return INDIGO_ACTION_NONE;
    }
    for (unsigned i = 0; i < main_count; i++) {
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

#define COL_LINK INDIGO_RGBA(112, 168, 255, 255)
#define COL_LIKED INDIGO_RGBA(255, 120, 150, 255)
#define COL_REPOSTED INDIGO_RGBA(120, 220, 160, 255)
#define POST_TEXT_SCALE 0.6f
#define POST_TEXT_X 18
#define POST_TEXT_Y 98
#define POST_LINE_PITCH 19
#define POST_TEXT_LINES 5

static const char *
author_name(const indigo_post *p)
{
    return p->display_name[0] ? p->display_name : p->handle;
}

/* Draw `text` as wrapped lines, colouring facet ranges. */
static void
draw_post_text(indigo_canvas *c, const indigo_post *p)
{
    indigo_line lines[POST_TEXT_LINES];
    int truncated;
    unsigned units = (unsigned) ((INDIGO_TOP_WIDTH - 2 * POST_TEXT_X) /
                                 (INDIGO_CHAR_WIDTH * POST_TEXT_SCALE));
    unsigned n = indigo_wrap(p->text, units, lines, POST_TEXT_LINES, &truncated);

    for (unsigned i = 0; i < n; i++) {
        const char *at = p->text + lines[i].start;
        unsigned len = lines[i].len;
        bool last = truncated && i + 1 == n;

        /* %.*s keeps this to the line; the ellipsis marks cut-off text. */
        indigo_canvas_text(c, POST_TEXT_X, POST_TEXT_Y + (float) (POST_LINE_PITCH * i),
                           POST_TEXT_SCALE, COL_TEXT, "%.*s%s", (int) len, at,
                           last ? "..." : "");
        for (unsigned f = 0; f < p->facet_count; f++) {
            unsigned s = p->facets[f].start;
            unsigned e = p->facets[f].end;

            if (e <= lines[i].start || s >= lines[i].start + len) {
                continue;
            }
            s = s < lines[i].start ? lines[i].start : s;
            e = e > lines[i].start + len ? lines[i].start + len : e;
            indigo_canvas_span(c, s - lines[i].start, e - lines[i].start, COL_LINK);
        }
    }
}

static void
build_top_post(const indigo_app *app, indigo_canvas *c)
{
    const indigo_timeline *t = &app->timeline;
    const indigo_post *p = indigo_timeline_selected(t);

    indigo_canvas_text(c, 18, 8, 0.8f, COL_TEXT, "Home");
    indigo_canvas_text(c, 100, 12, 0.5f, COL_TEXT_DIM, "A  Profile   START  Exit");
    if (p) {
        indigo_canvas_text(c, 330, 12, 0.6f, COL_TEXT_DIM, "%u / %u%s", t->selected + 1,
                           t->count, t->has_more ? "+" : "");
    }

    if (!p) {
        indigo_canvas_text(c, 18, 104, 0.8f, COL_TEXT_SOFT, "%s",
                           t->loading ? "Loading your timeline..." : "No posts to show.");
        if (t->status[0]) {
            indigo_canvas_text(c, 18, 136, 0.65f,
                               t->status_is_error ? COL_ERROR : COL_TEXT_DIM, "%s", t->status);
        }
        return;
    }

    if (p->reposted_by[0]) {
        indigo_canvas_text(c, 18, 36, 0.55f, COL_REPOSTED, "Reposted by %s", p->reposted_by);
    } else if (p->is_reply) {
        indigo_canvas_text(c, 18, 36, 0.55f, COL_TEXT_DIM, "Reply");
    }
    indigo_canvas_text(c, 18, 52, 0.75f, COL_TEXT, "%s", author_name(p));
    indigo_canvas_text(c, 18, 76, 0.55f, COL_TEXT_DIM, "@%s", p->handle);
    draw_post_text(c, p);

    if (p->embed_note[0]) {
        indigo_canvas_text(c, 18, 196, 0.55f, COL_TEXT_DIM, "%s", p->embed_note);
    }
    indigo_canvas_text(c, 18, 214, 0.55f, COL_TEXT_DIM, "%u replies", p->reply_count);
    indigo_canvas_text(c, 118, 214, 0.55f, p->repost_uri[0] ? COL_REPOSTED : COL_TEXT_DIM,
                       "%u reposts", p->repost_count);
    indigo_canvas_text(c, 218, 214, 0.55f, p->like_uri[0] ? COL_LIKED : COL_TEXT_DIM,
                       "%u likes", p->like_count);
    if (t->loading) {
        indigo_canvas_text(c, 330, 214, 0.55f, COL_TEXT_DIM, "Loading...");
    } else if (t->status[0]) {
        indigo_canvas_text(c, 18, 36, 0.55f, t->status_is_error ? COL_ERROR : COL_TEXT_DIM,
                           "%s", t->status);
    }
}

static void
build_top(const indigo_app *app, indigo_canvas *c)
{
    indigo_canvas_init(c, INDIGO_TOP_WIDTH, INDIGO_TOP_HEIGHT);
    indigo_canvas_rect(c, 0, 0, INDIGO_TOP_WIDTH, INDIGO_TOP_HEIGHT, COL_BG_TOP);

    if (app->screen == INDIGO_SCREEN_HOME) {
        indigo_canvas_rect(c, 0, 0, INDIGO_TOP_WIDTH, 32, COL_BAR);
        build_top_post(app, c);
        return;
    }

    indigo_canvas_rect(c, 0, 0, INDIGO_TOP_WIDTH, 46, COL_BAR);
    indigo_canvas_text(c, 18, 10, 1.0f, COL_TEXT, "Indigo");

    indigo_canvas_text(c, 18, 62, 0.7f, COL_TEXT_SOFT,
                       "Native Bluesky client");

    if (app->screen == INDIGO_SCREEN_SIGNIN) {
        build_top_signin(app, c);
        return;
    }

    indigo_canvas_text(c, 18, 104, 0.8f, COL_TEXT, "Profile");
    indigo_canvas_text(c, 18, 134, 0.65f, COL_TEXT_SOFT, "Signed in as %s",
                       app->signin.account);
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
action_pill(indigo_canvas *c, indigo_action action, bool on, bool busy, uint32_t on_color,
            const char *label)
{
    indigo_rect r = indigo_layout_button_rect(action);

    indigo_canvas_rect(c, r.x, r.y, r.w, r.h, on ? on_color : COL_PILL);
    indigo_canvas_text(c, r.x + 10, r.y + 7, 0.65f, busy ? COL_TEXT_DIM : COL_TEXT, "%s",
                       label);
}

static void
build_bottom_home(const indigo_app *app, indigo_canvas *c)
{
    const indigo_timeline *t = &app->timeline;
    const indigo_post *sel = indigo_timeline_selected(t);

    indigo_canvas_text(c, 14, 8, 0.75f, COL_TEXT, "Timeline");
    indigo_canvas_text(c, 150, 14, 0.55f, t->status_is_error ? COL_ERROR : COL_TEXT_DIM, "%s",
                       t->loading ? "Loading..." : t->status);

    for (unsigned row = 0; row < INDIGO_TIMELINE_ROWS; row++) {
        unsigned idx = t->scroll + row;
        indigo_action a = (indigo_action) (INDIGO_ACTION_ROW0 + row);
        indigo_rect r = indigo_layout_button_rect(a);
        const indigo_post *p;
        indigo_line line;
        int cut;
        unsigned units = (unsigned) ((ROW_W - 20) / (INDIGO_CHAR_WIDTH * 0.55f));

        if (idx >= t->count) {
            break;
        }
        p = &t->posts[idx];
        indigo_canvas_rect(c, r.x, r.y, r.w, r.h, idx == t->selected ? COL_PILL_ACTIVE : COL_PILL);
        indigo_canvas_text(c, r.x + 10, r.y + 3, 0.6f, COL_TEXT, "%s%.40s",
                           p->reposted_by[0] ? "RT  " : "", author_name(p));
        if (indigo_wrap(p->text, units, &line, 1, &cut) == 0) {
            line = (indigo_line) {0, 0};
        }
        indigo_canvas_text(c, r.x + 10, r.y + 24, 0.55f, COL_TEXT_SOFT, "%.*s%s", (int) line.len,
                           p->text + line.start, cut ? "..." : "");
    }

    action_pill(c, INDIGO_ACTION_LIKE, sel && sel->like_uri[0], sel && sel->like_pending,
                COL_PILL_ACTIVE, sel && sel->like_uri[0] ? "Y  Liked" : "Y  Like");
    action_pill(c, INDIGO_ACTION_REPOST, sel && sel->repost_uri[0], sel && sel->repost_pending,
                COL_PILL_ACTIVE, sel && sel->repost_uri[0] ? "X  Reposted" : "X  Repost");
    action_pill(c, INDIGO_ACTION_REFRESH, t->loading, false, COL_PILL_ACTIVE, "SEL  Refresh");
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
    if (app->screen == INDIGO_SCREEN_HOME) {
        build_bottom_home(app, c);
        return;
    }
    indigo_canvas_text(c, 14, 8, 0.75f, COL_TEXT, "Profile");
    indigo_canvas_text(c, 14, 52, 0.65f, COL_TEXT_SOFT, "Touch  x: %d  y: %d  %s",
                       input->touch_x, input->touch_y,
                       input->touch_down ? "down" : "up");

    pill(c, INDIGO_ACTION_HOME, false, "B  Back to timeline");
    pill(c, INDIGO_ACTION_SIGN_OUT, false, "Sign out");
}

void
indigo_layout_build(const indigo_app *app, const indigo_input *input,
                    indigo_canvas *top, indigo_canvas *bottom)
{
    build_top(app, top);
    build_bottom(app, input, bottom);
}
