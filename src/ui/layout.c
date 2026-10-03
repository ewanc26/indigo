#include "ui/layout.h"

#include "app/signin.h"
#include "ui/wrap.h"

#include <stdio.h>

#define COL_BG_TOP INDIGO_RGBA(18, 20, 26, 255)
#define COL_BG_BOTTOM INDIGO_RGBA(12, 14, 18, 255)
#define COL_BAR INDIGO_RGBA(30, 34, 44, 255)
#define COL_TEXT INDIGO_RGBA(255, 255, 255, 255)
#define COL_TEXT_SOFT INDIGO_RGBA(220, 224, 232, 255)
#define COL_TEXT_DIM INDIGO_RGBA(160, 168, 184, 255)
#define COL_PILL INDIGO_RGBA(44, 50, 66, 255)
#define COL_PILL_ACTIVE INDIGO_RGBA(74, 96, 180, 255)

/* Timeline-style lists: three rows with 4px between, then a row of four
 * action pills with 6px gaps. Every list screen shares them. */
#define ROW_X 8
#define ROW_W 304
#define ROW_H 46
#define ROW_STEP 50
#define ROW_Y0 48
#define PILL_Y 202
#define PILL_W 74
#define PILL_H 34
static const indigo_rect s_pill[4] = {
    {4, PILL_Y, PILL_W, PILL_H},
    {84, PILL_Y, PILL_W, PILL_H},
    {164, PILL_Y, PILL_W, PILL_H},
    {244, PILL_Y, PILL_W, PILL_H},
};

/* Top-right of the bottom screen's header bar: Back, or Menu on Home. */
static const indigo_rect s_back_button = {232, 4, 82, 34};

/* Menu: five full-width items. */
#define MENU_X 20
#define MENU_W 280
#define MENU_H 34
#define MENU_Y0 50
#define MENU_STEP 38

/* Compose: the draft box, the Reply/Quote switch and Post. */
static const indigo_rect s_edit_button = {14, 52, 292, 84};
static const indigo_rect s_toggle_button = {14, 144, 292, 36};
static const indigo_rect s_send_button = {14, 188, 292, 40};

/* Sign-in form: 8px between rows so a thumb never lands on two. */
static const indigo_rect s_field_service = {14, 52, 292, 38};
static const indigo_rect s_field_handle = {14, 98, 292, 38};
static const indigo_rect s_field_password = {14, 144, 292, 38};
static const indigo_rect s_sign_in_button = {14, 192, 292, 36};

/* Search: the query box sits in the header bar beside Back, so the result
 * rows keep the standard list geometry below it. */
static const indigo_rect s_query_button = {14, 6, 210, 30};

/* Profile: the follow button is the screen's one action, so it gets the full
 * width a compose box uses rather than one of the four post-screen pills,
 * which the profile does not otherwise need. */
static const indigo_rect s_follow_button = {14, 52, 292, 40};

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
        return s_pill[0];
    case INDIGO_ACTION_REPOST:
        return s_pill[1];
    case INDIGO_ACTION_OPEN:
    case INDIGO_ACTION_REPLY:
        return s_pill[2];
    case INDIGO_ACTION_REFRESH:
    case INDIGO_ACTION_AUTHOR:
        return s_pill[3];
    case INDIGO_ACTION_BACK:
    case INDIGO_ACTION_MENU:
        return s_back_button;
    case INDIGO_ACTION_MENU0:
    case INDIGO_ACTION_MENU1:
    case INDIGO_ACTION_MENU2:
    case INDIGO_ACTION_MENU3:
    case INDIGO_ACTION_MENU4:
        return (indigo_rect) {MENU_X, MENU_Y0 + MENU_STEP * (float) (action - INDIGO_ACTION_MENU0),
                              MENU_W, MENU_H};
    case INDIGO_ACTION_EDIT:
        return s_edit_button;
    case INDIGO_ACTION_FIELD_QUERY:
        return s_query_button;
    case INDIGO_ACTION_FOLLOW:
        return s_follow_button;
    case INDIGO_ACTION_TOGGLE:
        return s_toggle_button;
    case INDIGO_ACTION_SEND:
        return s_send_button;
    case INDIGO_ACTION_FIELD_SERVICE:
        return s_field_service;
    case INDIGO_ACTION_FIELD_HANDLE:
        return s_field_handle;
    case INDIGO_ACTION_FIELD_PASSWORD:
        return s_field_password;
    case INDIGO_ACTION_SIGN_IN:
        return s_sign_in_button;
    case INDIGO_ACTION_SIGN_OUT:
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
        INDIGO_ACTION_ROW0, INDIGO_ACTION_ROW1, INDIGO_ACTION_ROW2, INDIGO_ACTION_LIKE,
        INDIGO_ACTION_REPOST, INDIGO_ACTION_OPEN, INDIGO_ACTION_REFRESH, INDIGO_ACTION_MENU};
    static const indigo_action thread_actions[] = {
        INDIGO_ACTION_ROW0, INDIGO_ACTION_ROW1, INDIGO_ACTION_ROW2, INDIGO_ACTION_LIKE,
        INDIGO_ACTION_REPOST, INDIGO_ACTION_REPLY, INDIGO_ACTION_AUTHOR, INDIGO_ACTION_BACK};
    static const indigo_action profile_actions[] = {INDIGO_ACTION_FOLLOW, INDIGO_ACTION_BACK};
    static const indigo_action note_actions[] = {
        INDIGO_ACTION_ROW0, INDIGO_ACTION_ROW1, INDIGO_ACTION_ROW2, INDIGO_ACTION_OPEN,
        INDIGO_ACTION_REFRESH, INDIGO_ACTION_BACK};
    static const indigo_action menu_actions[] = {
        INDIGO_ACTION_MENU0, INDIGO_ACTION_MENU1, INDIGO_ACTION_MENU2, INDIGO_ACTION_MENU3,
        INDIGO_ACTION_MENU4, INDIGO_ACTION_BACK};
    static const indigo_action compose_actions[] = {
        INDIGO_ACTION_EDIT, INDIGO_ACTION_TOGGLE, INDIGO_ACTION_SEND, INDIGO_ACTION_BACK};
    /* AUTHOR is the same pill rect as REFRESH and means the same thing here:
     * open the selected person's profile. */
    static const indigo_action search_actions[] = {
        INDIGO_ACTION_FIELD_QUERY, INDIGO_ACTION_ROW0, INDIGO_ACTION_ROW1,
        INDIGO_ACTION_ROW2, INDIGO_ACTION_AUTHOR, INDIGO_ACTION_BACK};
    const indigo_action *list = signin_actions;
    unsigned count = 0;

#define USE(arr) (list = (arr), count = sizeof(arr) / sizeof((arr)[0]))
    switch (screen) {
    case INDIGO_SCREEN_SIGNIN:
        USE(signin_actions);
        break;
    case INDIGO_SCREEN_HOME:
        USE(home_actions);
        break;
    case INDIGO_SCREEN_THREAD:
        USE(thread_actions);
        break;
    case INDIGO_SCREEN_PROFILE:
        USE(profile_actions);
        break;
    case INDIGO_SCREEN_NOTIFICATIONS:
        USE(note_actions);
        break;
    case INDIGO_SCREEN_MENU:
        USE(menu_actions);
        break;
    case INDIGO_SCREEN_COMPOSE:
        USE(compose_actions);
        break;
    case INDIGO_SCREEN_SEARCH:
        USE(search_actions);
        break;
    }
#undef USE
    for (unsigned i = 0; i < count; i++) {
        if (inside(indigo_layout_button_rect(list[i]), touch_x, touch_y)) {
            return list[i];
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
    bool thread = app->screen == INDIGO_SCREEN_THREAD;
    const indigo_timeline *t = thread ? &app->thread : &app->timeline;
    const indigo_post *p = indigo_timeline_selected(t);

    indigo_canvas_text(c, 18, 8, 0.8f, COL_TEXT, thread ? "Thread" : "Home");
    /* The bottom pills carry the hints for the list actions (Y, X, A, SEL), so
     * the title bar only states what they cannot: leaving the screen. */
    indigo_canvas_text(c, 116, 12, 0.5f, COL_TEXT_DIM, "%s",
                       thread            ? "B  Back   SEL  Profile"
                       : "B  Menu   SEL  Reload   START  Exit");
    if (p) {
        indigo_canvas_text(c, 330, 12, 0.6f, COL_TEXT_DIM, "%u / %u%s", t->selected + 1,
                           t->count, t->has_more ? "+" : "");
    }

    if (!p) {
        indigo_canvas_text(c, 18, 104, 0.8f, COL_TEXT_SOFT, "%s",
                           t->loading ? (thread ? "Loading the thread..." : "Loading your timeline...")
                       : "No posts to show.");
        if (t->status[0]) {
            indigo_canvas_text(c, 18, 136, 0.65f,
                               t->status_is_error ? COL_ERROR : COL_TEXT_DIM, "%s", t->status);
        }
        return;
    }

    if (thread) {
        indigo_canvas_text(c, 18, 36, 0.55f, COL_TEXT_DIM, "%s",
                           t->selected < app->thread_focus    ? "Earlier in the thread"
                           : t->selected == app->thread_focus ? "The post"
                                                               : "A reply");
    } else if (p->reposted_by[0]) {
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
        indigo_canvas_text(c, 190, 36, 0.55f, t->status_is_error ? COL_ERROR : COL_TEXT_DIM,
                           "%s", t->status);
    }
}

static void
top_title(indigo_canvas *c, const char *title, const char *hint)
{
    indigo_canvas_rect(c, 0, 0, INDIGO_TOP_WIDTH, 32, COL_BAR);
    indigo_canvas_text(c, 18, 8, 0.8f, COL_TEXT, "%s", title);
    indigo_canvas_text(c, 190, 12, 0.5f, COL_TEXT_DIM, "%s", hint);
}

/* Wrap `text` into the top screen at (x, y); returns the next free y. */
static float
top_paragraph(indigo_canvas *c, float x, float y, float scale, uint32_t color,
              unsigned max_lines, const char *text)
{
    indigo_line lines[8];
    int truncated;
    unsigned units = (unsigned) ((INDIGO_TOP_WIDTH - 2 * x) / (INDIGO_CHAR_WIDTH * scale));
    unsigned n;

    if (max_lines > 8) {
        max_lines = 8;
    }
    n = indigo_wrap(text, units, lines, max_lines, &truncated);
    for (unsigned i = 0; i < n; i++) {
        indigo_canvas_text(c, x, y, scale, color, "%.*s%s", (int) lines[i].len,
                           text + lines[i].start, truncated && i + 1 == n ? "..." : "");
        y += (float) POST_LINE_PITCH * scale / POST_TEXT_SCALE * 0.95f;
    }
    return y;
}

static void
build_top_profile(const indigo_app *app, indigo_canvas *c)
{
    const indigo_profile *p = &app->profile;

    top_title(c, "Profile", "Y  Follow");
    if (!p->loaded) {
        indigo_canvas_text(c, 18, 60, 0.75f, COL_TEXT_SOFT, "%s",
                           p->loading ? "Loading profile..." : "Profile not loaded.");
        if (p->handle[0]) {
            indigo_canvas_text(c, 18, 90, 0.6f, COL_TEXT_DIM, "@%s", p->handle);
        }
        if (p->status[0]) {
            indigo_canvas_text(c, 18, 116, 0.6f, p->status_is_error ? COL_ERROR : COL_TEXT_DIM,
                               "%s", p->status);
        }
        return;
    }
    indigo_canvas_text(c, 18, 44, 0.85f, COL_TEXT, "%.30s",
                       p->display_name[0] ? p->display_name : p->handle);
    indigo_canvas_text(c, 18, 72, 0.6f, COL_TEXT_DIM, "@%s%s", p->handle,
                       p->following ? "   Following" : "");
    top_paragraph(c, 18, 98, 0.6f, COL_TEXT_SOFT, 4, p->bio);
    indigo_canvas_text(c, 18, 196, 0.6f, COL_TEXT, "%u posts", p->posts);
    indigo_canvas_text(c, 128, 196, 0.6f, COL_TEXT, "%u followers", p->followers);
    indigo_canvas_text(c, 262, 196, 0.6f, COL_TEXT, "%u following", p->follows);
}

static const char *
note_verb(indigo_note_kind k)
{
    switch (k) {
    case INDIGO_NOTE_LIKE:
        return "liked your post";
    case INDIGO_NOTE_REPOST:
        return "reposted your post";
    case INDIGO_NOTE_FOLLOW:
        return "followed you";
    case INDIGO_NOTE_REPLY:
        return "replied to you";
    case INDIGO_NOTE_MENTION:
        return "mentioned you";
    case INDIGO_NOTE_QUOTE:
        return "quoted your post";
    case INDIGO_NOTE_OTHER:
        break;
    }
    return "did something";
}

static void
build_top_notifications(const indigo_app *app, indigo_canvas *c)
{
    const indigo_notifications *n = &app->notifications;
    const indigo_notification *sel = indigo_notifications_selected(n);

    top_title(c, "Notifications", "B  Back   SEL  Reload");
    if (!sel) {
        indigo_canvas_text(c, 18, 90, 0.75f, COL_TEXT_SOFT, "%s",
                           n->loading ? "Loading notifications..." : "Nothing to show.");
        if (n->status[0]) {
            indigo_canvas_text(c, 18, 120, 0.6f, n->status_is_error ? COL_ERROR : COL_TEXT_DIM,
                               "%s", n->status);
        }
        return;
    }
    indigo_canvas_text(c, 330, 36, 0.55f, COL_TEXT_DIM, "%u / %u", n->selected + 1, n->count);
    indigo_canvas_text(c, 18, 48, 0.75f, COL_TEXT, "%.28s", sel->name[0] ? sel->name : sel->handle);
    indigo_canvas_text(c, 18, 74, 0.6f, COL_TEXT_DIM, "@%s", sel->handle);
    indigo_canvas_text(c, 18, 96, 0.65f, sel->unread ? COL_LINK : COL_TEXT_SOFT, "%s%s",
                       note_verb(sel->kind), sel->unread ? "  (new)" : "");
    if (sel->text[0]) {
        top_paragraph(c, 18, 124, 0.6f, COL_TEXT, 4, sel->text);
    }
}

static void
build_top_search(const indigo_app *app, indigo_canvas *c)
{
    const indigo_search *s = &app->search;
    const indigo_actor *sel = indigo_search_selected(s);

    top_title(c, "Search", "A  Type   SEL  Open");
    if (s->loading) {
        indigo_canvas_text(c, 18, 90, 0.75f, COL_TEXT_SOFT, "Searching...");
        return;
    }
    if (!sel) {
        if (s->status[0]) {
            indigo_canvas_text(c, 18, 84, 0.7f, s->status_is_error ? COL_ERROR : COL_TEXT_SOFT,
                               "%.40s", s->status);
        } else if (!s->searched) {
            indigo_canvas_text(c, 18, 76, 0.7f, COL_TEXT_SOFT,
                               "Find people by name or handle.");
            indigo_canvas_text(c, 18, 104, 0.6f, COL_TEXT_DIM,
                               "Press A, or tap the box, to type.");
        } else {
            indigo_canvas_text(c, 18, 90, 0.7f, COL_TEXT_SOFT, "No results.");
        }
        return;
    }
    indigo_canvas_text(c, 330, 36, 0.55f, COL_TEXT_DIM, "%u / %u", s->selected + 1, s->count);
    indigo_canvas_text(c, 18, 52, 0.85f, COL_TEXT, "%.30s",
                       sel->display_name[0] ? sel->display_name : sel->handle);
    indigo_canvas_text(c, 18, 82, 0.65f, COL_TEXT_DIM, "@%s", sel->handle);
    indigo_canvas_text(c, 18, 112, 0.55f, COL_TEXT_DIM, "%.44s", sel->did);
    indigo_canvas_text(c, 18, 140, 0.6f, COL_TEXT_SOFT, "SEL  Open profile");
}

static void
build_top_menu(const indigo_app *app, indigo_canvas *c)
{
    top_title(c, "Menu", "B  Close");
    indigo_canvas_text(c, 18, 64, 0.7f, COL_TEXT_SOFT, "Signed in as");
    indigo_canvas_text(c, 18, 90, 0.8f, COL_TEXT, "%s", app->signin.account);
    indigo_canvas_text(c, 18, 150, 0.6f, COL_TEXT_DIM, "Wolfram: %s",
                       app->wolfram_linked ? "linked" : "not linked");
    indigo_canvas_text(c, 18, 208, 0.6f, COL_TEXT_DIM, "START  Exit");
}

static unsigned
utf8_length(const char *s)
{
    unsigned n = 0;

    for (; *s; s++) {
        if (((unsigned char) *s & 0xC0) != 0x80) {
            n++;
        }
    }
    return n;
}

static const char *
compose_title(const indigo_compose *c)
{
    return c->mode == INDIGO_COMPOSE_REPLY   ? "Reply"
           : c->mode == INDIGO_COMPOSE_QUOTE ? "Quote post"
                                             : "New post";
}

static void
build_top_compose(const indigo_app *app, indigo_canvas *c)
{
    const indigo_compose *d = &app->compose;
    float y = 44;

    top_title(c, compose_title(d), "A  Write   B  Back");
    if (d->has_target) {
        indigo_canvas_text(c, 18, y, 0.55f, COL_TEXT_DIM, "%s @%s",
                           d->mode == INDIGO_COMPOSE_QUOTE ? "Quoting" : "Replying to",
                           d->target.handle);
        y = top_paragraph(c, 18, y + 18, 0.55f, COL_TEXT_DIM, 2, d->target.text) + 8;
    }
    if (d->text[0]) {
        top_paragraph(c, 18, y + 4, 0.65f, COL_TEXT, 5, d->text);
    } else {
        indigo_canvas_text(c, 18, y + 4, 0.65f, COL_TEXT_DIM, "Tap the box below to write.");
    }
    indigo_canvas_text(c, 18, 214, 0.55f, utf8_length(d->text) > 300 ? COL_ERROR : COL_TEXT_DIM,
                       "%u / 300", utf8_length(d->text));
    if (d->status[0]) {
        indigo_canvas_text(c, 120, 214, 0.55f, d->status_is_error ? COL_ERROR : COL_TEXT_SOFT,
                           "%.50s", d->status);
    } else if (d->sending) {
        indigo_canvas_text(c, 120, 214, 0.55f, COL_TEXT_SOFT, "Posting...");
    }
}

static void
build_top(const indigo_app *app, indigo_canvas *c)
{
    indigo_canvas_init(c, INDIGO_TOP_WIDTH, INDIGO_TOP_HEIGHT);
    indigo_canvas_rect(c, 0, 0, INDIGO_TOP_WIDTH, INDIGO_TOP_HEIGHT, COL_BG_TOP);

    if (app->screen == INDIGO_SCREEN_HOME || app->screen == INDIGO_SCREEN_THREAD) {
        indigo_canvas_rect(c, 0, 0, INDIGO_TOP_WIDTH, 32, COL_BAR);
        build_top_post(app, c);
        return;
    }
    switch (app->screen) {
    case INDIGO_SCREEN_PROFILE:
        build_top_profile(app, c);
        return;
    case INDIGO_SCREEN_NOTIFICATIONS:
        build_top_notifications(app, c);
        return;
    case INDIGO_SCREEN_MENU:
        build_top_menu(app, c);
        return;
    case INDIGO_SCREEN_COMPOSE:
        build_top_compose(app, c);
        return;
    case INDIGO_SCREEN_SEARCH:
        build_top_search(app, c);
        return;
    default:
        break;
    }

    indigo_canvas_rect(c, 0, 0, INDIGO_TOP_WIDTH, 46, COL_BAR);
    indigo_canvas_text(c, 18, 10, 1.0f, COL_TEXT, "Indigo");

    indigo_canvas_text(c, 18, 62, 0.7f, COL_TEXT_SOFT,
                       "Native Bluesky client");

    if (app->screen == INDIGO_SCREEN_SIGNIN) {
        build_top_signin(app, c);
        return;
    }
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
    indigo_canvas_text(c, r.x + 8, r.y + 9, 0.55f, busy ? COL_TEXT_DIM : COL_TEXT, "%s",
                       label);
}

static void
back_button(indigo_canvas *c, indigo_action action, const char *label)
{
    indigo_rect r = indigo_layout_button_rect(action);

    indigo_canvas_rect(c, r.x, r.y, r.w, r.h, COL_PILL);
    indigo_canvas_text(c, r.x + 10, r.y + 7, 0.6f, COL_TEXT, "%s", label);
}

static void
list_row(indigo_canvas *c, indigo_action a, bool selected, const char *title, const char *body)
{
    indigo_rect r = indigo_layout_button_rect(a);
    unsigned units = (unsigned) ((ROW_W - 20) / (INDIGO_CHAR_WIDTH * 0.55f));
    indigo_line line;
    int cut;

    indigo_canvas_rect(c, r.x, r.y, r.w, r.h, selected ? COL_PILL_ACTIVE : COL_PILL);
    indigo_canvas_text(c, r.x + 10, r.y + 3, 0.6f, COL_TEXT, "%s", title);
    if (indigo_wrap(body, units, &line, 1, &cut) == 0) {
        line = (indigo_line) {0, 0};
    }
    indigo_canvas_text(c, r.x + 10, r.y + 24, 0.55f, COL_TEXT_SOFT, "%.*s%s", (int) line.len,
                       body + line.start, cut ? "..." : "");
}

static void
build_bottom_posts(const indigo_app *app, indigo_canvas *c)
{
    bool thread = app->screen == INDIGO_SCREEN_THREAD;
    const indigo_timeline *t = thread ? &app->thread : &app->timeline;
    const indigo_post *sel = indigo_timeline_selected(t);

    indigo_canvas_text(c, 14, 8, 0.75f, COL_TEXT, thread ? "Thread" : "Timeline");
    indigo_canvas_text(c, 118, 14, 0.5f, t->status_is_error ? COL_ERROR : COL_TEXT_DIM, "%.22s",
                       t->loading ? "Loading..." : t->status);
    /* The button hints live in the top screen's title bar; a bottom pill is
     * labelled with its action so no back hint appears twice. */
    back_button(c, thread ? INDIGO_ACTION_BACK : INDIGO_ACTION_MENU, thread ? "Back" : "Menu");

    for (unsigned row = 0; row < INDIGO_TIMELINE_ROWS; row++) {
        unsigned idx = t->scroll + row;
        const indigo_post *p;
        char title[96];

        if (idx >= t->count) {
            break;
        }
        p = &t->posts[idx];
        snprintf(title, sizeof title, "%s%.32s", thread && idx < app->thread_focus ? "^  "
                                                 : thread && idx > app->thread_focus ? "> "
                                                 : p->reposted_by[0] ? "RT  " : "",
                 author_name(p));
        list_row(c, (indigo_action) (INDIGO_ACTION_ROW0 + row), idx == t->selected, title,
                 p->text);
    }

    action_pill(c, INDIGO_ACTION_LIKE, sel && sel->like_uri[0], sel && sel->like_pending,
                COL_PILL_ACTIVE, sel && sel->like_uri[0] ? "Y Liked" : "Y Like");
    action_pill(c, INDIGO_ACTION_REPOST, sel && sel->repost_uri[0], sel && sel->repost_pending,
                COL_PILL_ACTIVE, sel && sel->repost_uri[0] ? "X Reposted" : "X Repost");
    if (thread) {
        action_pill(c, INDIGO_ACTION_REPLY, false, false, COL_PILL_ACTIVE, "A Reply");
        action_pill(c, INDIGO_ACTION_AUTHOR, false, false, COL_PILL_ACTIVE, "Profile");
    } else {
        action_pill(c, INDIGO_ACTION_OPEN, false, false, COL_PILL_ACTIVE, "A Open");
        action_pill(c, INDIGO_ACTION_REFRESH, t->loading, false, COL_PILL_ACTIVE, "Reload");
    }
}

static void
build_bottom_profile(const indigo_app *app, indigo_canvas *c)
{
    const indigo_profile *p = &app->profile;
    indigo_rect f = indigo_layout_button_rect(INDIGO_ACTION_FOLLOW);

    indigo_canvas_text(c, 14, 8, 0.75f, COL_TEXT, "Profile");
    back_button(c, INDIGO_ACTION_BACK, "Back");

    /* Greyed until the profile has loaded: following needs the did, which only
     * the profile response carries. */
    indigo_canvas_rect(c, f.x, f.y, f.w, f.h,
                       p->loaded && !p->loading && !p->follow_busy ? COL_PILL_ACTIVE : COL_PILL);
    indigo_canvas_text(c, f.x + 100, f.y + 12, 0.7f,
                       p->loaded && !p->follow_busy ? COL_TEXT : COL_TEXT_DIM, "%s",
                       !p->loaded              ? "Loading..."
                       : p->follow_busy        ? (p->following ? "Following..." : "Unfollowing...")
                       : p->following          ? "Following"
                                               : "Follow");

    if (p->status[0] && p->loaded) {
        indigo_canvas_text(c, 14, 110, 0.6f, p->status_is_error ? COL_ERROR : COL_TEXT_SOFT,
                           "%.44s", p->status);
    }
}

static void
build_bottom_notifications(const indigo_app *app, indigo_canvas *c)
{
    const indigo_notifications *n = &app->notifications;

    indigo_canvas_text(c, 14, 8, 0.75f, COL_TEXT, "Notifications");
    back_button(c, INDIGO_ACTION_BACK, "Back");
    for (unsigned row = 0; row < INDIGO_TIMELINE_ROWS; row++) {
        unsigned idx = n->scroll + row;
        const indigo_notification *it;
        char title[96];

        if (idx >= n->count) {
            break;
        }
        it = &n->items[idx];
        snprintf(title, sizeof title, "%s%.30s", it->unread ? "* " : "",
                 it->name[0] ? it->name : it->handle);
        list_row(c, (indigo_action) (INDIGO_ACTION_ROW0 + row), idx == n->selected, title,
                 it->text[0] ? it->text : note_verb(it->kind));
    }
    action_pill(c, INDIGO_ACTION_OPEN, false, false, COL_PILL_ACTIVE, "A Open");
    action_pill(c, INDIGO_ACTION_REFRESH, n->loading, false, COL_PILL_ACTIVE, "Reload");
}

static void
build_bottom_menu(const indigo_app *app, indigo_canvas *c)
{
    char position[32];

    indigo_canvas_text(c, 14, 8, 0.75f, COL_TEXT, "Menu");
    back_button(c, INDIGO_ACTION_BACK, "Close");

    snprintf(position, sizeof position, "%u of %u", app->menu.selected + 1, app->menu.count);
    indigo_canvas_text(c, 14, 26, 0.55f, COL_TEXT_DIM, "%s", position);

    for (unsigned i = 0; i < INDIGO_MENU_ROWS; i++) {
        const indigo_menu_item *it = indigo_menu_row(&app->menu, i, INDIGO_MENU_ROWS);
        indigo_rect r = indigo_layout_button_rect((indigo_action) (INDIGO_ACTION_MENU0 + i));
        bool active = it && app->menu.selected == app->menu.scroll + i;

        if (!it) {
            continue;
        }
        indigo_canvas_rect(c, r.x, r.y, r.w, r.h, active ? COL_PILL_ACTIVE : COL_PILL);
        indigo_canvas_text(c, r.x + 14, r.y + 8, 0.7f, COL_TEXT, "%s", it->label);
    }
}

static void
build_bottom_compose(const indigo_app *app, indigo_canvas *c)
{
    const indigo_compose *d = &app->compose;
    indigo_rect e = indigo_layout_button_rect(INDIGO_ACTION_EDIT);
    indigo_rect t = indigo_layout_button_rect(INDIGO_ACTION_TOGGLE);
    indigo_rect s = indigo_layout_button_rect(INDIGO_ACTION_SEND);
    bool can_toggle = indigo_compose_can_toggle(d);

    indigo_canvas_text(c, 14, 8, 0.75f, COL_TEXT, "%s", compose_title(d));
    back_button(c, INDIGO_ACTION_BACK, "Back");

    indigo_canvas_rect(c, e.x, e.y, e.w, e.h, COL_PILL);
    if (d->text[0]) {
        indigo_line lines[3];
        int cut;
        unsigned units = (unsigned) ((e.w - 20) / (INDIGO_CHAR_WIDTH * 0.6f));
        unsigned n = indigo_wrap(d->text, units, lines, 3, &cut);

        for (unsigned i = 0; i < n; i++) {
            indigo_canvas_text(c, e.x + 10, e.y + 8 + 22 * (float) i, 0.6f, COL_TEXT, "%.*s%s",
                               (int) lines[i].len, d->text + lines[i].start,
                               cut && i + 1 == n ? "..." : "");
        }
    } else {
        indigo_canvas_text(c, e.x + 10, e.y + 30, 0.7f, COL_TEXT_DIM, "Tap to write");
    }

    indigo_canvas_rect(c, t.x, t.y, t.w, t.h, can_toggle ? COL_PILL_ACTIVE : COL_PILL);
    indigo_canvas_text(c, t.x + 14, t.y + 9, 0.6f, can_toggle ? COL_TEXT : COL_TEXT_DIM, "%s",
                       !d->has_target                        ? "Plain post"
                       : d->mode == INDIGO_COMPOSE_QUOTE ? "Quoting - Y switch to reply"
                                                              : "Replying - Y switch to quote");

    indigo_canvas_rect(c, s.x, s.y, s.w, s.h, indigo_compose_ready(d) ? COL_PILL_ACTIVE : COL_PILL);
    indigo_canvas_text(c, s.x + 110, s.y + 9, 0.7f,
                       indigo_compose_ready(d) ? COL_TEXT : COL_TEXT_DIM, "%s",
                       d->sending ? "Posting..." : "R  Post");
}

static void
build_bottom_search(const indigo_app *app, indigo_canvas *c)
{
    const indigo_search *s = &app->search;
    indigo_rect q = indigo_layout_button_rect(INDIGO_ACTION_FIELD_QUERY);

    indigo_canvas_rect(c, q.x, q.y, q.w, q.h, s->query[0] ? COL_PILL_ACTIVE : COL_PILL);
    indigo_canvas_text(c, q.x + 8, q.y + 8, 0.55f, s->query[0] ? COL_TEXT : COL_TEXT_DIM, "%s",
                       s->query[0] ? s->query : "Tap to type a name");
    back_button(c, INDIGO_ACTION_BACK, "Back");

    for (unsigned row = 0; row < INDIGO_SEARCH_ROWS; row++) {
        unsigned idx = s->scroll + row;
        const indigo_actor *it = indigo_search_row(s, row, INDIGO_SEARCH_ROWS);
        char title[96];

        if (!it) {
            break;
        }
        snprintf(title, sizeof title, "%.30s",
                 it->display_name[0] ? it->display_name : it->handle);
        list_row(c, (indigo_action) (INDIGO_ACTION_ROW0 + row), idx == s->selected, title,
                 it->handle);
    }
    /* Only one pill: typing is the header box, and a second "A Type" pill at
     * the bottom would either overlap that box or need an action that means
     * something else to the touch handler. Same label as the thread screen's
     * author pill: SEL opens a profile there too, and "SEL Open" does not fit
     * a 74px pill in the baked font. */
    action_pill(c, INDIGO_ACTION_AUTHOR, indigo_search_selected(s) != NULL, s->loading,
                COL_PILL_ACTIVE, "Profile");
}

static void
build_bottom(const indigo_app *app, const indigo_input *input, indigo_canvas *c)
{
    (void) input;
    indigo_canvas_init(c, INDIGO_BOTTOM_WIDTH, INDIGO_BOTTOM_HEIGHT);
    indigo_canvas_rect(c, 0, 0, INDIGO_BOTTOM_WIDTH, INDIGO_BOTTOM_HEIGHT, COL_BG_BOTTOM);
    indigo_canvas_rect(c, 0, 0, INDIGO_BOTTOM_WIDTH, 42, COL_BAR);
    switch (app->screen) {
    case INDIGO_SCREEN_SIGNIN:
        build_bottom_signin(app, c);
        break;
    case INDIGO_SCREEN_HOME:
    case INDIGO_SCREEN_THREAD:
        build_bottom_posts(app, c);
        break;
    case INDIGO_SCREEN_PROFILE:
        build_bottom_profile(app, c);
        break;
    case INDIGO_SCREEN_NOTIFICATIONS:
        build_bottom_notifications(app, c);
        break;
    case INDIGO_SCREEN_MENU:
        build_bottom_menu(app, c);
        break;
    case INDIGO_SCREEN_COMPOSE:
        build_bottom_compose(app, c);
        break;
    case INDIGO_SCREEN_SEARCH:
        build_bottom_search(app, c);
        break;
    }
}

void
indigo_layout_build(const indigo_app *app, const indigo_input *input,
                    indigo_canvas *top, indigo_canvas *bottom)
{
    build_top(app, top);
    build_bottom(app, input, bottom);
}
