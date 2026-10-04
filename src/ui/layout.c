#include "ui/layout.h"

#include "app/signin.h"
#include "atproto/session.h"
#include "media/media.h"
#include "ui/wrap.h"
#include "util/buildinfo.h"

#include <stdio.h>
#include <string.h>

#define COL_BG_TOP INDIGO_RGBA(18, 20, 26, 255)
#define COL_BG_BOTTOM INDIGO_RGBA(12, 14, 18, 255)
#define COL_BAR INDIGO_RGBA(30, 34, 44, 255)
#define COL_TEXT INDIGO_RGBA(255, 255, 255, 255)
#define COL_TEXT_SOFT INDIGO_RGBA(220, 224, 232, 255)
#define COL_TEXT_DIM INDIGO_RGBA(160, 168, 184, 255)
#define COL_PILL INDIGO_RGBA(44, 50, 66, 255)
/* A link card is a surface, not a control: one step above the background, so
 * it reads as a card the reader can look into rather than something to press. */
#define COL_CARD INDIGO_RGBA(27, 31, 41, 255)
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
static const indigo_rect s_follow_button = {14, 44, 292, 36};
/* Mute and block sit under Follow as a pair of halves, so the moderation
 * actions read as one group rather than two more full-width bars. */
static const indigo_rect s_mute_button = {14, 86, 142, 34};
static const indigo_rect s_block_button = {164, 86, 142, 34};
/* Followers and following open the people list, so they carry the counts the
 * profile already holds rather than being bare navigation labels. */
static const indigo_rect s_followers_button = {14, 126, 142, 34};
static const indigo_rect s_following_button = {164, 126, 142, 34};
/* Posts and the pinned post share the last row. The bottom screen is 240 tall
 * and every row above is spoken for, so this is a pair rather than two full
 * width bars, and the status line has to fit below it. */
static const indigo_rect s_posts_button = {14, 166, 142, 34};
static const indigo_rect s_pinned_button = {164, 166, 142, 34};
/* 206 + one 0.6-scale text line clears the 240-tall bottom screen. The
 * spacing test below pins that the button rows leave room for it. */
#define PROFILE_STATUS_Y 206

/* Settings rows: eight options on the bottom screen.
 *
 * Eight rows of the original 24px with a 27px step reach 257 on a 240-tall
 * screen, so the rows gave up 2px of height and the first row moved up to clear
 * the Back button by the same margin it always had. The 3px gap is unchanged,
 * because the gap is what decides whether a thumb can land on the wrong row.
 * Making the screen scrollable is the answer that compromises none of this, and
 * it is the answer to reach at a ninth row rather than an eighth. */
#define SETTINGS_ROW_X 14
#define SETTINGS_ROW_W 292
#define SETTINGS_ROW_H 22
#define SETTINGS_ROW_Y0 41
#define SETTINGS_ROW_STEP 25

indigo_rect
indigo_layout_button_rect(indigo_action action)
{
    switch (action) {
    case INDIGO_ACTION_ROW0:
    case INDIGO_ACTION_ROW1:
    case INDIGO_ACTION_ROW2:
        return (indigo_rect) {ROW_X, ROW_Y0 + ROW_STEP * (float) (action - INDIGO_ACTION_ROW0),
                              ROW_W, ROW_H};
    case INDIGO_ACTION_SETTINGS_ROW0:
    case INDIGO_ACTION_SETTINGS_ROW1:
    case INDIGO_ACTION_SETTINGS_ROW2:
    case INDIGO_ACTION_SETTINGS_ROW3:
    case INDIGO_ACTION_SETTINGS_ROW4:
    case INDIGO_ACTION_SETTINGS_ROW5:
    case INDIGO_ACTION_SETTINGS_ROW7:
    case INDIGO_ACTION_SETTINGS_ROW6:
        return (indigo_rect) {SETTINGS_ROW_X,
                              SETTINGS_ROW_Y0 + SETTINGS_ROW_STEP * (float) (action - INDIGO_ACTION_SETTINGS_ROW0),
                              SETTINGS_ROW_W, SETTINGS_ROW_H};
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
    case INDIGO_ACTION_MUTE:
        return s_mute_button;
    case INDIGO_ACTION_BLOCK:
        return s_block_button;
    case INDIGO_ACTION_FOLLOWERS:
        return s_followers_button;
    case INDIGO_ACTION_FOLLOWING:
        return s_following_button;
    case INDIGO_ACTION_POSTS:
        return s_posts_button;
    case INDIGO_ACTION_PINNED:
        return s_pinned_button;
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

typedef struct {
    uint32_t bg_top;
    uint32_t bg_bottom;
    uint32_t bar;
    uint32_t text;
    uint32_t text_soft;
    uint32_t text_dim;
    uint32_t pill;
    uint32_t pill_active;
} indigo_palette;

static indigo_palette
indigo_layout_palette(const indigo_settings *s)
{
    if (s && s->high_contrast) {
        return (indigo_palette) {
            .bg_top = INDIGO_RGBA(0, 0, 0, 255),
            .bg_bottom = INDIGO_RGBA(0, 0, 0, 255),
            .bar = INDIGO_RGBA(40, 44, 56, 255),
            .text = INDIGO_RGBA(255, 255, 255, 255),
            .text_soft = INDIGO_RGBA(245, 245, 250, 255),
            .text_dim = INDIGO_RGBA(210, 215, 230, 255),
            .pill = INDIGO_RGBA(30, 35, 50, 255),
            .pill_active = INDIGO_RGBA(90, 120, 255, 255),
        };
    }
    if (s && s->theme == INDIGO_THEME_LIGHT) {
        return (indigo_palette) {
            .bg_top = INDIGO_RGBA(240, 242, 248, 255),
            .bg_bottom = INDIGO_RGBA(230, 234, 242, 255),
            .bar = INDIGO_RGBA(210, 216, 230, 255),
            .text = INDIGO_RGBA(15, 20, 30, 255),
            .text_soft = INDIGO_RGBA(45, 55, 75, 255),
            .text_dim = INDIGO_RGBA(90, 100, 120, 255),
            .pill = INDIGO_RGBA(195, 205, 225, 255),
            .pill_active = INDIGO_RGBA(60, 90, 210, 255),
        };
    }
    return (indigo_palette) {
        .bg_top = COL_BG_TOP,
        .bg_bottom = COL_BG_BOTTOM,
        .bar = COL_BAR,
        .text = COL_TEXT,
        .text_soft = COL_TEXT_SOFT,
        .text_dim = COL_TEXT_DIM,
        .pill = COL_PILL,
        .pill_active = COL_PILL_ACTIVE,
    };
}

static bool
inside(indigo_rect r, int x, int y, bool large_targets)
{
    float margin = large_targets ? 4.0f : 0.0f;

    return (float) x >= r.x - margin && (float) x < r.x + r.w + margin &&
           (float) y >= r.y - margin && (float) y < r.y + r.h + margin;
}

indigo_action
indigo_layout_hit_settings(indigo_screen screen, bool large_targets, int touch_x,
                           int touch_y)
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
    static const indigo_action profile_actions[] = {
    INDIGO_ACTION_FOLLOW, INDIGO_ACTION_MUTE, INDIGO_ACTION_BLOCK,
    INDIGO_ACTION_FOLLOWERS, INDIGO_ACTION_FOLLOWING, INDIGO_ACTION_POSTS,
    INDIGO_ACTION_PINNED, INDIGO_ACTION_BACK};
    static const indigo_action note_actions[] = {
        INDIGO_ACTION_ROW0, INDIGO_ACTION_ROW1, INDIGO_ACTION_ROW2, INDIGO_ACTION_OPEN,
        INDIGO_ACTION_REFRESH, INDIGO_ACTION_BACK};
    static const indigo_action menu_actions[] = {
        INDIGO_ACTION_MENU0, INDIGO_ACTION_MENU1, INDIGO_ACTION_MENU2, INDIGO_ACTION_MENU3,
        INDIGO_ACTION_MENU4, INDIGO_ACTION_BACK};
    static const indigo_action compose_actions[] = {
        INDIGO_ACTION_EDIT, INDIGO_ACTION_TOGGLE, INDIGO_ACTION_SEND, INDIGO_ACTION_BACK};
    static const indigo_action search_actions[] = {
        INDIGO_ACTION_FIELD_QUERY, INDIGO_ACTION_ROW0, INDIGO_ACTION_ROW1,
        INDIGO_ACTION_ROW2, INDIGO_ACTION_AUTHOR, INDIGO_ACTION_BACK};
    static const indigo_action settings_actions[] = {
        INDIGO_ACTION_SETTINGS_ROW0, INDIGO_ACTION_SETTINGS_ROW1,
        INDIGO_ACTION_SETTINGS_ROW2, INDIGO_ACTION_SETTINGS_ROW3,
        INDIGO_ACTION_SETTINGS_ROW4, INDIGO_ACTION_SETTINGS_ROW5,
        INDIGO_ACTION_SETTINGS_ROW6, INDIGO_ACTION_SETTINGS_ROW7,
        INDIGO_ACTION_BACK};
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
    case INDIGO_SCREEN_SETTINGS:
        USE(settings_actions);
        break;
    }
#undef USE
    for (unsigned i = 0; i < count; i++) {
        if (inside(indigo_layout_button_rect(list[i]), touch_x, touch_y, large_targets)) {
            return list[i];
        }
    }
    return INDIGO_ACTION_NONE;
}

indigo_action
indigo_layout_hit_app(const indigo_app *app, int touch_x, int touch_y)
{
    return indigo_layout_hit_settings(app ? app->screen : INDIGO_SCREEN_SIGNIN,
                                      app ? app->settings.large_targets : false,
                                      touch_x, touch_y);
}

indigo_action
indigo_layout_hit(indigo_screen screen, int touch_x, int touch_y)
{
    return indigo_layout_hit_settings(screen, false, touch_x, touch_y);
}

#define COL_ERROR INDIGO_RGBA(255, 138, 128, 255)

static void
build_top_signin(const indigo_app *app, indigo_canvas *c)
{
    const indigo_signin *s = &app->signin;

    indigo_canvas_text(c, 18, 98, 0.8f, COL_TEXT, "Sign in");
    indigo_canvas_text(c, 18, 126, 0.6f, COL_TEXT_DIM,
                       "Empty password = browser sign-in.");
    indigo_canvas_text(c, 18, 144, 0.6f, COL_TEXT_DIM,
                       "The PDS handles your password + MFA.");

#if defined(__3DS__)
    if (indigo_session_pair_url()[0]) {
        const char *url = indigo_session_pair_url();
        const char *code = indigo_session_pair_code();
        indigo_canvas_text(c, 18, 166, 0.55f, COL_TEXT_SOFT,
                           "Open on your phone/computer:");
        indigo_canvas_text(c, 18, 182, 0.45f, COL_TEXT,
                           "%.120s", url);
        indigo_canvas_text(c, 18, 196, 0.55f, COL_TEXT_SOFT,
                           "Pair code: %s", code);
    } else
#endif
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

/* Avatar sizes. The row size is what a 304x46 row can give up without
 * squeezing the two text lines; the header sizes are the top screen's. */
#define ROW_AVATAR 32
#define HEAD_AVATAR 24

/* A post's embed -- an image, or a link card -- takes the space between the
 * text and the counters, so the text drops to POST_TEXT_LINES_EMBED lines.
 *
 * The other way to do it is a thumbnail beside the text, and that is worse: it
 * costs about 130px of width, which is nearly half the characters per line, so
 * a post's worth of text would not fit in the space the thumbnail would have
 * taken. Two lines of caption and a real box for the picture is the better
 * trade on a screen this size. */
#define POST_TEXT_LINES_EMBED 2
#define POST_NOTE_Y 196
#define POST_COUNTER_Y 214
#define EMBED_X 18
#define EMBED_Y 132
#define EMBED_W 364
#define EMBED_H 60
/* Alt text: the scale it is drawn at, and the picture height kept when it has
 * taken lines out of the band. A 4:3 photo at the floor is still 40x30. */
#define ALT_SCALE 0.55f
#define EMBED_H_MIN 30.0f
/* A link card's text sits inside its own edge; the inset is the padding. */
#define CARD_INSET 10
/* The thumbnail inside a link card, and the space kept clear for it. */
#define CARD_THUMB 52
#define PROFILE_AVATAR 40

/* A square image at `size` pixels, or a tinted placeholder while it loads.
 *
 * The tint comes from the URL, so the same account is the same colour
 * everywhere it appears and two accounts never look alike: a column of
 * identical grey squares reads as one voice, which is the wrong thing for a
 * timeline to say. It is the only colour on screen not drawn from the palette,
 * and that is deliberate -- it has to be distinguishable per account. */
static void
draw_avatar(indigo_canvas *c, const char *url, float x, float y, float size)
{
    if (!url || !url[0]) {
        return;
    }
    indigo_canvas_image(c, x, y, size, size, url, indigo_media_placeholder_color(url));
}

/* Draw `text` as wrapped lines, colouring facet ranges. `max_lines` is the
 * budget the caller has: a post with an embed gives up three of its five. */
static void
draw_post_text(indigo_canvas *c, const indigo_post *p, unsigned max_lines)
{
    indigo_line lines[POST_TEXT_LINES];
    int truncated;
    unsigned units = (unsigned) ((INDIGO_TOP_WIDTH - 2 * POST_TEXT_X) /
                                 (INDIGO_CHAR_WIDTH * POST_TEXT_SCALE));
    unsigned n = indigo_wrap(p->text, units, lines, max_lines, &truncated);

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

/* True when the post has an embed the screen draws as itself rather than as a
 * line of text. A quote, a video and an attachment Indigo cannot draw all keep
 * the one-line note instead. */
static bool
post_draws_embed(const indigo_post *p)
{
    if (p->embed_kind == INDIGO_EMBED_IMAGE) {
        return p->embed_thumb[0] != '\0';
    }
    if (p->embed_kind == INDIGO_EMBED_LINK) {
        return p->embed_uri[0] != '\0';
    }
    return false;
}

/* A box of the image's own shape inside `band_h` of the band, centred on it.
 * The aspect ratio is the server's; when it declared none, a square is the
 * assumption that distorts least, because the box drawn is stretched to
 * whatever shape it is given. */
static void
draw_post_image(indigo_canvas *c, const indigo_post *p, float band_h)
{
    float aw = p->embed_w > 0 && p->embed_h > 0 ? (float) p->embed_w : 1.0f;
    float ah = p->embed_w > 0 && p->embed_h > 0 ? (float) p->embed_h : 1.0f;
    /* Fit the declared shape inside the band on both axes. Scaling by the
     * smaller of the two ratios is what keeps a 1:3 panorama from being 180px
     * tall: the box is drawn stretched to whatever shape it is given, so a box
     * outside the band is a box off the screen. */
    float k = EMBED_W / aw;
    float by_height = band_h / ah;
    float w;
    float h;

    if (by_height < k) {
        k = by_height;
    }
    w = aw * k;
    h = ah * k;
    indigo_canvas_image(c, EMBED_X + (EMBED_W - w) / 2.0f, EMBED_Y, w, h,
                        p->embed_thumb, indigo_media_placeholder_color(p->embed_thumb));
    /* Bluesky allows four images and Indigo draws one, so a post of several says
     * so rather than quietly showing one of them as if it were all of them. */
    if (p->embed_count > 1) {
        /* On the band's own bottom edge, so it moves up with the picture when
         * alt text has taken lines out of it. */
        indigo_canvas_text(c, 330, EMBED_Y + band_h - 8.0f, ALT_SCALE, COL_TEXT_DIM,
                           "+%u more", (unsigned) p->embed_count - 1u);
    }
}

/* Alt text under a picture, when the setting is on and the author wrote any.
 *
 * It takes what it needs rather than a fixed share: one sentence costs one line,
 * a long description costs the two that fit, and the picture is fitted into
 * whatever is left of the band. A setting that made every photograph a quarter
 * of the screen smaller would not be worth turning on.
 *
 * Alt text is drawn in the dim colour and at the small scale deliberately. It
 * is reference, not caption: the post's own text is the thing being read, and
 * an accessibility option that competes with it is not one. */
static float
draw_post_alt(indigo_canvas *c, const char *alt)
{
    static const float pitch = 13.0f;
    indigo_line lines[2];
    int truncated;
    unsigned units = (unsigned) (EMBED_W / (INDIGO_CHAR_WIDTH * ALT_SCALE));
    unsigned n = indigo_wrap(alt, units, lines, 2, &truncated);

    for (unsigned i = 0; i < n; i++) {
        indigo_canvas_text(c, EMBED_X,
                           EMBED_Y + EMBED_H - (float) (n - i) * pitch, ALT_SCALE,
                           COL_TEXT_DIM, "%.*s%s", (int) lines[i].len,
                           alt + lines[i].start, truncated && i + 1 == n ? "..." : "");
    }
    return (float) n * pitch;
}

/* A link card: the title over the place it points at, with the link's own
 * picture on the right when the card has one. Indigo cannot open a link yet, so
 * this is something to read rather than something to press -- hence a surface
 * colour rather than a control's. */
static void
draw_post_link(indigo_canvas *c, const indigo_post *p)
{
    float text_x = EMBED_X + (float) CARD_INSET;
    float text_w = EMBED_W - 2.0f * (float) CARD_INSET;
    const char *title = p->embed_title[0] ? p->embed_title : p->embed_uri;
    indigo_line lines[2];
    int truncated;
    unsigned units;
    unsigned n;
    float y;

    if (p->embed_thumb[0]) {
        text_w -= (float) (CARD_THUMB + CARD_INSET);
    }
    indigo_canvas_rect(c, EMBED_X, EMBED_Y, EMBED_W, EMBED_H, COL_CARD);
    units = (unsigned) (text_w / (INDIGO_CHAR_WIDTH * POST_TEXT_SCALE));
    if (units == 0) {
        units = 1;
    }
    n = indigo_wrap(title, units, lines, 2, &truncated);

    for (unsigned i = 0; i < n; i++) {
        indigo_canvas_text(c, text_x,
                           EMBED_Y + (float) CARD_INSET +
                               (float) POST_LINE_PITCH * (float) i,
                           POST_TEXT_SCALE, COL_TEXT, "%.*s%s", (int) lines[i].len,
                           title + lines[i].start,
                           truncated && i + 1 == n ? "..." : "");
    }
    /* The title, when there was one, sits above the URI it belongs to. */
    y = EMBED_Y + (float) CARD_INSET + (float) POST_LINE_PITCH * (float) n;
    indigo_canvas_text(c, text_x, y, 0.55f, COL_TEXT_DIM, "%.38s", p->embed_uri);
    if (p->embed_thumb[0]) {
        indigo_canvas_image(c, EMBED_X + EMBED_W - (float) (CARD_INSET + CARD_THUMB),
                            EMBED_Y + (float) (CARD_INSET - 4), CARD_THUMB, CARD_THUMB,
                            p->embed_thumb,
                            indigo_media_placeholder_color(p->embed_thumb));
    }
}

/* The post's text and its embed, filling the space between the header and the
 * counters. An embed that cannot be drawn keeps its one-line note -- a quote, a
 * video, an attachment -- and an embed that can be drawn does not, because the
 * note would say less than the picture does. */
static void
draw_post_body(indigo_canvas *c, const indigo_post *p, bool show_alt)
{
    bool embed = post_draws_embed(p);
    float band_h = EMBED_H;

    draw_post_text(c, p, embed ? POST_TEXT_LINES_EMBED : POST_TEXT_LINES);
    if (embed) {
        if (p->embed_kind != INDIGO_EMBED_IMAGE) {
            draw_post_link(c, p);
            return;
        }
        if (show_alt && p->embed_alt[0]) {
            /* The alt text sits at the bottom of the band, so the picture is
             * fitted into what is above it. */
            float used = draw_post_alt(c, p->embed_alt);

            band_h = EMBED_H - used - 4.0f;
            if (band_h < EMBED_H_MIN) {
                band_h = EMBED_H_MIN;
            }
        }
        draw_post_image(c, p, band_h);
        return;
    }
    if (p->embed_note[0]) {
        indigo_canvas_text(c, POST_TEXT_X, POST_NOTE_Y, 0.55f, COL_TEXT_DIM, "%s",
                           p->embed_note);
    }
}

static void
build_top_post(const indigo_app *app, indigo_canvas *c)
{
    bool thread = app->screen == INDIGO_SCREEN_THREAD;
    const indigo_timeline *t = thread ? &app->thread : &app->timeline;
    const indigo_post *p = indigo_timeline_selected(t);

    const char *title = thread ? "Thread" : app->feed_uri[0] ? app->feed_name : "Home";
    /* A feed name is longer than "Home", and the counter above the rows wants
     * the right of the bar, so the feed hint drops the START reminder to keep
     * room for the name: START still exits, as it does on every list screen. */
    const char *hint = thread             ? "B  Back   SEL  Profile"
                        : app->feed_uri[0] ? "B  Feeds   SEL  Reload"
                        : "B  Menu   SEL  Reload   START  Exit";
    float hint_x = 18.0f + (float) strlen(title) * INDIGO_CHAR_WIDTH * 0.8f + 12.0f;
    float hint_end = 326.0f - (float) strlen(hint) * INDIGO_CHAR_WIDTH * 0.5f;
    unsigned units;

    if (hint_x < 116.0f) {
        hint_x = 116.0f;
    }
    if (hint_x > hint_end && hint_end > 116.0f) {
        hint_x = hint_end;
    }
    /* A name that would still reach the hint is cut, and marked as cut, rather
     * than drawn over it. */
    units = (unsigned) ((hint_x - 26.0f) / (INDIGO_CHAR_WIDTH * 0.8f));
    if (strlen(title) > units) {
        indigo_canvas_text(c, 18, 8, 0.8f, COL_TEXT, "%.*s...", (int) units - 3, title);
    } else {
        indigo_canvas_text(c, 18, 8, 0.8f, COL_TEXT, "%s", title);
    }
    /* The bottom pills carry the hints for the list actions (Y, X, A, SEL), so
     * the title bar only states what they cannot: leaving the screen. */
    indigo_canvas_text(c, hint_x, 12, 0.5f, COL_TEXT_DIM, "%s", hint);
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
    draw_avatar(c, p->avatar, 12, 50, HEAD_AVATAR);
    /* The name and handle sit beside the avatar rather than above the text:
     * the text block is a fixed five lines starting at POST_TEXT_Y, and there
     * is no room to give the header a third line. */
    indigo_canvas_text(c, 44, 52, 0.75f, COL_TEXT, "%s", author_name(p));
    indigo_canvas_text(c, 44, 76, 0.55f, COL_TEXT_DIM, "@%s", p->handle);
    draw_post_body(c, p, app->settings.alt_text);

    indigo_canvas_text(c, 18, POST_COUNTER_Y, 0.55f, COL_TEXT_DIM, "%u replies",
                       p->reply_count);
    indigo_canvas_text(c, 118, POST_COUNTER_Y, 0.55f,
                       p->repost_uri[0] ? COL_REPOSTED : COL_TEXT_DIM, "%u reposts",
                       p->repost_count);
    indigo_canvas_text(c, 218, POST_COUNTER_Y, 0.55f,
                       p->like_uri[0] ? COL_LIKED : COL_TEXT_DIM, "%u likes",
                       p->like_count);
    if (t->loading) {
        indigo_canvas_text(c, 330, POST_COUNTER_Y, 0.55f, COL_TEXT_DIM, "Loading...");
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

    top_title(c, "Profile", "Y  Follow   X  Mute   R  Block");
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
    draw_avatar(c, p->avatar, 14, 38, PROFILE_AVATAR);
    /* The display name is cut to 30 characters because a 40px avatar plus the
     * longest name that still fits is narrower than the full 400px line. */
    indigo_canvas_text(c, 64, 44, 0.85f, COL_TEXT, "%.26s",
                       p->display_name[0] ? p->display_name : p->handle);
    indigo_canvas_text(c, 64, 72, 0.6f, COL_TEXT_DIM, "@%s%s", p->handle,
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
    draw_avatar(c, sel->avatar, 12, 46, HEAD_AVATAR);
    indigo_canvas_text(c, 44, 48, 0.75f, COL_TEXT, "%.28s", sel->name[0] ? sel->name : sel->handle);
    indigo_canvas_text(c, 44, 74, 0.6f, COL_TEXT_DIM, "@%s", sel->handle);
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
    const indigo_post *psel = indigo_search_selected_post(s);
    const indigo_list *lsel = indigo_search_selected_list(s);
    bool posts = indigo_search_is_posts(s);
    bool lists = indigo_search_is_lists(s);

    /* SEL opens whatever the row is: a profile for a person, a thread for a
     * post, a member list for a curated list. All read as "Open" here, so the
     * hint is stated once. The lists have nothing to type, so the A hint is
     * only offered where the header box takes typing. */
    top_title(c, indigo_search_title(s),
              indigo_search_is_typed(s) ? "A  Type   SEL  Open" : "SEL  Open");
    if (s->loading) {
        indigo_canvas_text(c, 18, 90, 0.75f, COL_TEXT_SOFT, "Searching...");
        return;
    }
    if (posts ? !psel : lists ? !lsel : !sel) {
        if (s->status[0]) {
            indigo_canvas_text(c, 18, 84, 0.7f, s->status_is_error ? COL_ERROR : COL_TEXT_SOFT,
                               "%.40s", s->status);
        } else if (!s->searched) {
            /* Reached only for the two search modes: every other kind is
             * requested on open, so those arrive loading, loaded or failed. */
            indigo_canvas_text(c, 18, 76, 0.7f, COL_TEXT_SOFT,
                               posts ? "Search posts by words in their text."
                                     : "Find people by name or handle.");
            if (indigo_search_is_typed(s)) {
                indigo_canvas_text(c, 18, 104, 0.6f, COL_TEXT_DIM,
                                   "Press A, or tap the box, to type.");
            }
        } else {
            indigo_canvas_text(c, 18, 90, 0.7f, COL_TEXT_SOFT, "No results.");
        }
        return;
    }
    indigo_canvas_text(c, 330, 36, 0.55f, COL_TEXT_DIM, "%u / %u", s->selected + 1, s->count);
    if (lists && s->kind == INDIGO_SEARCH_LISTS) {
        indigo_canvas_text(c, 18, 52, 0.85f, COL_TEXT, "%.30s", lsel->name);
        if (lsel->description[0]) {
            top_paragraph(c, 18, 82, 0.6f, COL_TEXT_SOFT, 3, lsel->description);
        }
        indigo_canvas_text(c, 18, 196, 0.55f, COL_TEXT_DIM, "%.44s", lsel->uri);
        indigo_canvas_text(c, 18, 214, 0.6f, COL_TEXT_SOFT, "SEL  Open members");
        return;
    }
    if (lists && s->kind == INDIGO_SEARCH_FEEDS) {
        indigo_canvas_text(c, 18, 52, 0.85f, COL_TEXT, "%.30s", lsel->name);
        indigo_canvas_text(c, 18, 82, 0.6f, COL_TEXT_SOFT, "Custom feed");
        indigo_canvas_text(c, 18, 196, 0.55f, COL_TEXT_DIM, "%.44s", lsel->uri);
        indigo_canvas_text(c, 18, 214, 0.6f, COL_TEXT_SOFT, "SEL  Open feed");
        return;
    }
    if (posts) {
        indigo_canvas_text(c, 18, 52, 0.75f, COL_TEXT, "%.30s",
                           author_name(psel));
        indigo_canvas_text(c, 18, 76, 0.55f, COL_TEXT_DIM, "@%s", psel->handle);
        draw_post_body(c, psel, app->settings.alt_text);
        indigo_canvas_text(c, 18, POST_COUNTER_Y, 0.55f, COL_TEXT_DIM, "%u replies",
                           psel->reply_count);
        indigo_canvas_text(c, 118, POST_COUNTER_Y, 0.55f, COL_TEXT_DIM, "%u reposts",
                           psel->repost_count);
        indigo_canvas_text(c, 218, POST_COUNTER_Y, 0.55f, COL_TEXT_DIM, "%u likes",
                           psel->like_count);
        return;
    }
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
    /* The pill on the bottom screen is the control; this is the reading of it,
     * where there is room to spell the rule out. */
    if (indigo_compose_can_gate(d)) {
        indigo_canvas_text(c, 18, 188, 0.6f, COL_TEXT_DIM, "Replies: %s",
                           indigo_compose_gate_label(d));
    }
    if (d->status[0]) {
        indigo_canvas_text(c, 120, 214, 0.55f, d->status_is_error ? COL_ERROR : COL_TEXT_SOFT,
                           "%.50s", d->status);
    } else if (d->sending) {
        indigo_canvas_text(c, 120, 214, 0.55f, COL_TEXT_SOFT, "Posting...");
    }
}

static void
build_top_settings(const indigo_app *app, indigo_canvas *c)
{
    const indigo_settings *s = &app->settings;
    top_title(c, "Settings", "");

    const char *theme_str =
        s->theme == INDIGO_THEME_LIGHT ? "Light"
        : s->theme == INDIGO_THEME_DARK ? "Dark"
                                        : "Auto (follows system)";
    indigo_canvas_text(c, 18, 44, 0.65f, COL_TEXT, "Theme: %s", theme_str);
    indigo_canvas_text(c, 18, 66, 0.65f, COL_TEXT, "Text scale: %u%%", s->text_scale);
    indigo_canvas_text(c, 18, 88, 0.65f, COL_TEXT, "Reduce motion: %s",
                       s->reduce_motion ? "On" : "Off");
    indigo_canvas_text(c, 18, 110, 0.65f, COL_TEXT, "High contrast: %s",
                       s->high_contrast ? "On" : "Off");
    indigo_canvas_text(c, 18, 132, 0.65f, COL_TEXT, "Large touch targets: %s",
                       s->large_targets ? "On" : "Off");
    indigo_canvas_text(c, 18, 154, 0.65f, COL_TEXT, "Image alt text: %s",
                       s->alt_text ? "On" : "Off");
    indigo_canvas_text(c, 18, 176, 0.65f, COL_TEXT, "Diagnostics log: %s",
                       s->diagnostics ? "On (indigo.log)" : "Off");

    const char *feed_str = s->default_feed[0] ? s->default_feed : "Following timeline";
    indigo_canvas_text(c, 18, 198, 0.65f, COL_TEXT, "Startup feed: %.35s", feed_str);

    indigo_canvas_text(c, 18, 218, 0.55f, COL_TEXT_DIM,
                       "Press A or touch an item below to change.");

    /* The build identity, so a bug report can name the build it came from. All
     * three stamped values, because each answers a different question: the tag
     * says which release, the number orders two builds of one tag, and the date
     * says which day. Nothing else in the app prints them, which until now
     * meant the Makefile stamped three strings the linker then dropped. */
    indigo_canvas_text(c, 18, 232, 0.5f, COL_TEXT_DIM, "%s (build %d, %s)",
                       INDIGO_BUILD_COMMIT, INDIGO_BUILD_NUMBER, INDIGO_BUILD_DATE);
}

static void
build_top(const indigo_app *app, indigo_canvas *c)
{
    indigo_palette pal = indigo_layout_palette(&app->settings);

    indigo_canvas_init(c, INDIGO_TOP_WIDTH, INDIGO_TOP_HEIGHT);
    indigo_canvas_rect(c, 0, 0, INDIGO_TOP_WIDTH, INDIGO_TOP_HEIGHT, pal.bg_top);

    if (app->screen == INDIGO_SCREEN_HOME || app->screen == INDIGO_SCREEN_THREAD) {
        indigo_canvas_rect(c, 0, 0, INDIGO_TOP_WIDTH, 32, pal.bar);
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
    case INDIGO_SCREEN_SETTINGS:
        build_top_settings(app, c);
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

/* One list row: a pill, a title line, a body line and, when there is one, the
 * author's avatar. The avatar is drawn into every kind of row rather than each
 * screen doing its own, so a person looks the same in the timeline, in search
 * results, in a list of followers and in notifications. */
static void
list_row(indigo_canvas *c, indigo_action a, bool selected, const char *title,
         const char *body, const char *avatar)
{
    indigo_rect r = indigo_layout_button_rect(a);
    float text_x = r.x + 10;
    unsigned units;
    indigo_line line;
    int cut;

    indigo_canvas_rect(c, r.x, r.y, r.w, r.h, selected ? COL_PILL_ACTIVE : COL_PILL);
    if (avatar && avatar[0]) {
        draw_avatar(c, avatar, r.x + 7, r.y + 7, ROW_AVATAR);
        text_x = r.x + 7 + ROW_AVATAR + 8;
    }
    /* The wrap width follows the text's left edge, so an indented row is
     * truncated as tightly as an unindented one rather than running under the
     * row's right edge. */
    units = (unsigned) ((r.x + ROW_W - 10 - text_x) / (INDIGO_CHAR_WIDTH * 0.55f));
    indigo_canvas_text(c, text_x, r.y + 3, 0.6f, COL_TEXT, "%s", title);
    if (indigo_wrap(body, units, &line, 1, &cut) == 0) {
        line = (indigo_line) {0, 0};
    }
    indigo_canvas_text(c, text_x, r.y + 24, 0.55f, COL_TEXT_SOFT, "%.*s%s", (int) line.len,
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
     * labelled with its action so no back hint appears twice. A feed view's B
     * goes back to the picker, so its button is named for the picker. */
    back_button(c, thread ? INDIGO_ACTION_BACK : INDIGO_ACTION_MENU,
                thread ? "Back" : app->feed_uri[0] ? "Feeds" : "Menu");

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
                 p->text, p->avatar);
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

/* A toggle pair: the button names the action it performs, not the state it is
 * in, so the label never has to change under the reader. */
static void
toggle_button(indigo_canvas *c, indigo_action action, bool enabled, bool busy,
              const char *on_label, const char *off_label)
{
    indigo_rect r = indigo_layout_button_rect(action);

    indigo_canvas_rect(c, r.x, r.y, r.w, r.h, enabled && !busy ? COL_PILL_ACTIVE : COL_PILL);
    indigo_canvas_text(c, r.x + 10, r.y + 11, 0.6f, enabled && !busy ? COL_TEXT : COL_TEXT_DIM,
                       "%s", busy ? "..." : (enabled ? off_label : on_label));
}

static void
build_bottom_profile(const indigo_app *app, indigo_canvas *c)
{
    const indigo_profile *p = &app->profile;
    bool ready = p->loaded && !p->loading;

    indigo_canvas_text(c, 14, 8, 0.75f, COL_TEXT, "Profile");
    back_button(c, INDIGO_ACTION_BACK, "Back");

    /* Greyed until the profile has loaded: follow, mute and block all address
     * the subject by did, which only the profile response carries. */
    indigo_canvas_rect(c, s_follow_button.x, s_follow_button.y, s_follow_button.w,
                       s_follow_button.h,
                       ready && !p->follow_busy ? COL_PILL_ACTIVE : COL_PILL);
    indigo_canvas_text(c, s_follow_button.x + 100, s_follow_button.y + 12, 0.7f,
                       ready && !p->follow_busy ? COL_TEXT : COL_TEXT_DIM, "%s",
                       !p->loaded           ? "Loading..."
                       : p->follow_busy     ? (p->following ? "Following..." : "Unfollowing...")
                       : p->following       ? "Following"
                                             : "Follow");

    toggle_button(c, INDIGO_ACTION_MUTE, ready, p->mute_busy, "Unmute", "Mute");
    toggle_button(c, INDIGO_ACTION_BLOCK, ready, p->block_busy, "Unblock", "Block");

    /* The counts the profile already holds, as the way into the lists. */
    if (ready) {
        indigo_rect fl = indigo_layout_button_rect(INDIGO_ACTION_FOLLOWERS);
        indigo_rect fg = indigo_layout_button_rect(INDIGO_ACTION_FOLLOWING);

        indigo_canvas_rect(c, fl.x, fl.y, fl.w, fl.h, COL_PILL);
        indigo_canvas_text(c, fl.x + 10, fl.y + 5, 0.55f, COL_TEXT_SOFT, "Followers");
        indigo_canvas_text(c, fl.x + 10, fl.y + 21, 0.7f, COL_TEXT, "%u", p->followers);

        indigo_canvas_rect(c, fg.x, fg.y, fg.w, fg.h, COL_PILL);
        indigo_canvas_text(c, fg.x + 10, fg.y + 5, 0.55f, COL_TEXT_SOFT, "Following");
        indigo_canvas_text(c, fg.x + 10, fg.y + 21, 0.7f, COL_TEXT, "%u", p->follows);

        indigo_rect pb = indigo_layout_button_rect(INDIGO_ACTION_POSTS);

        indigo_canvas_rect(c, pb.x, pb.y, pb.w, pb.h, COL_PILL_ACTIVE);
        indigo_canvas_text(c, pb.x + 10, pb.y + 11, 0.7f, COL_TEXT, "Posts");
        /* Only meaningful when the profile actually has one, which is what
         * disables it rather than drawing an empty label. */
        action_pill(c, INDIGO_ACTION_PINNED, p->pinned_uri[0] != '\0', false, COL_PILL_ACTIVE,
                    "Pinned");
    }

    if (p->status[0] && p->loaded) {
        indigo_canvas_text(c, 14, PROFILE_STATUS_Y, 0.6f,
                           p->status_is_error ? COL_ERROR : COL_TEXT_SOFT, "%.44s", p->status);
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
                 it->text[0] ? it->text : note_verb(it->kind), it->avatar);
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

    /* With no target there is no reply/quote to switch, so the pill carries the
     * reply gate instead and Y cycles that. No "Replies:" prefix here: the top
     * screen already names it, and the longest wording has to fit this width. */
    if (indigo_compose_can_gate(d)) {
        indigo_canvas_rect(c, t.x, t.y, t.w, t.h, COL_PILL_ACTIVE);
        indigo_canvas_text(c, t.x + 14, t.y + 9, 0.6f, COL_TEXT, "%s - Y switch",
                           indigo_compose_gate_short(d));
    } else {
        indigo_canvas_rect(c, t.x, t.y, t.w, t.h, can_toggle ? COL_PILL_ACTIVE : COL_PILL);
        indigo_canvas_text(c, t.x + 14, t.y + 9, 0.6f, can_toggle ? COL_TEXT : COL_TEXT_DIM, "%s",
                           !d->has_target                        ? "Plain post"
                           : d->mode == INDIGO_COMPOSE_QUOTE ? "Quoting - Y switch to reply"
                                                              : "Replying - Y switch to quote");
    }

    indigo_canvas_rect(c, s.x, s.y, s.w, s.h, indigo_compose_ready(d) ? COL_PILL_ACTIVE : COL_PILL);
    indigo_canvas_text(c, s.x + 110, s.y + 9, 0.7f,
                       indigo_compose_ready(d) ? COL_TEXT : COL_TEXT_DIM, "%s",
                       d->sending ? "Posting..." : "R  Post");
}

static void
build_bottom_search(const indigo_app *app, indigo_canvas *c)
{
    const indigo_search *s = &app->search;
    bool posts = indigo_search_is_posts(s);
    bool opens;
    indigo_rect q = indigo_layout_button_rect(INDIGO_ACTION_FIELD_QUERY);

    indigo_canvas_rect(c, q.x, q.y, q.w, q.h, s->query[0] ? COL_PILL_ACTIVE : COL_PILL);
    if (indigo_search_is_typed(s)) {
        const char *hint = posts ? "Tap to type words" : "Tap to type a name";

        indigo_canvas_text(c, q.x + 8, q.y + 8, 0.55f,
                           s->query[0] ? COL_TEXT : COL_TEXT_DIM, "%s",
                           s->query[0] ? s->query : hint);
    } else {
        /* The followers and following lists have no query; the box names whose
         * list this is instead of inviting typing nothing would act on. */
        indigo_canvas_rect(c, q.x, q.y, q.w, q.h, COL_PILL);
        indigo_canvas_text(c, q.x + 8, q.y + 8, 0.55f, COL_TEXT_SOFT, "@%.40s", s->subject);
    }
    back_button(c, INDIGO_ACTION_BACK, "Back");

    for (unsigned row = 0; row < INDIGO_SEARCH_ROWS; row++) {
        unsigned idx = s->scroll + row;
        char title[96];

        if (s->kind == INDIGO_SEARCH_LISTS || s->kind == INDIGO_SEARCH_FEEDS) {
            const indigo_list *l = indigo_search_row_list(s, row, INDIGO_SEARCH_ROWS);

            if (!l) {
                break;
            }
            /* A list row shows its description where a person row shows a
             * handle: it is the one line that says what the list is for. */
            snprintf(title, sizeof title, "%.30s", l->name);
            list_row(c, (indigo_action) (INDIGO_ACTION_ROW0 + row), idx == s->selected, title,
                     l->description[0] ? l->description : "No description", NULL);
            continue;
        }
        if (indigo_search_is_posts(s)) {
            const indigo_post *p = indigo_search_row_post(s, row, INDIGO_SEARCH_ROWS);

            if (!p) {
                break;
            }
            /* A post row leads with its author, because the text is too long
             * for one row to be identifiable by. */
            snprintf(title, sizeof title, "%.30s",
                     p->display_name[0] ? p->display_name : p->handle);
            list_row(c, (indigo_action) (INDIGO_ACTION_ROW0 + row), idx == s->selected, title,
                     p->text, p->avatar);
            continue;
        }
        {
            const indigo_actor *it = indigo_search_row(s, row, INDIGO_SEARCH_ROWS);

            if (!it) {
                break;
            }
            snprintf(title, sizeof title, "%.30s",
                     it->display_name[0] ? it->display_name : it->handle);
            list_row(c, (indigo_action) (INDIGO_ACTION_ROW0 + row), idx == s->selected, title,
                     it->handle, it->avatar);
        }
    }
    /* Only one pill: typing is the header box, and a second "A Type" pill at
     * the bottom would either overlap that box or need an action that means
     * something else to the touch handler. Same label as the thread screen's
     * author pill: SEL opens a profile there too, and "SEL Open" does not fit
     * a 74px pill in the baked font. Every row kind but the people ones opens
     * its row rather than a profile: a thread, a curated list, a saved feed. */
    opens = posts || indigo_search_is_lists(s);
    action_pill(c, INDIGO_ACTION_AUTHOR,
                opens ? (posts ? indigo_search_selected_post(s) != NULL
                               : indigo_search_selected_list(s) != NULL)
                      : indigo_search_selected(s) != NULL,
                s->loading, COL_PILL_ACTIVE,
                posts ? "Thread" : opens ? "Open" : "Profile");
}

static void
build_bottom_settings(const indigo_app *app, indigo_canvas *c)
{
    const indigo_settings *s = &app->settings;
    indigo_canvas_text(c, 14, 10, 0.9f, COL_TEXT, "Settings");
    back_button(c, INDIGO_ACTION_BACK, "Back");

    static const char *labels[8] = {
        "Theme", "Text scale", "Reduce motion", "High contrast",
        "Large targets", "Image alt text", "Diagnostics log", "Startup feed"
    };

    for (unsigned i = 0; i < 8; i++) {
        indigo_action act = (indigo_action) (INDIGO_ACTION_SETTINGS_ROW0 + i);
        indigo_rect r = indigo_layout_button_rect(act);
        bool active = (app->settings_selected == i);
        char val[64];

        switch (i) {
        case 0:
            snprintf(val, sizeof val, "%s",
                     s->theme == INDIGO_THEME_LIGHT ? "Light"
                     : s->theme == INDIGO_THEME_DARK ? "Dark" : "Auto");
            break;
        case 1:
            snprintf(val, sizeof val, "%u%%", s->text_scale);
            break;
        case 2:
            snprintf(val, sizeof val, "%s", s->reduce_motion ? "On" : "Off");
            break;
        case 3:
            snprintf(val, sizeof val, "%s", s->high_contrast ? "On" : "Off");
            break;
        case 4:
            snprintf(val, sizeof val, "%s", s->large_targets ? "On" : "Off");
            break;
        case 5:
            snprintf(val, sizeof val, "%s", s->alt_text ? "On" : "Off");
            break;
        case 6:
            snprintf(val, sizeof val, "%s", s->diagnostics ? "On" : "Off");
            break;
        case 7:
            if (s->default_feed[0]) {
                snprintf(val, sizeof val, "Custom feed");
            } else if (app->feed_uri[0]) {
                snprintf(val, sizeof val, "Set current");
            } else {
                snprintf(val, sizeof val, "Following");
            }
            break;
        }

        indigo_canvas_rect(c, r.x, r.y, r.w, r.h, active ? COL_PILL_ACTIVE : COL_PILL);
        indigo_canvas_text(c, r.x + 8, r.y + 4, 0.6f, COL_TEXT, "%s", labels[i]);
        indigo_canvas_text(c, r.x + r.w - 100, r.y + 4, 0.6f, COL_TEXT_SOFT, "%s", val);
    }
}

static void
build_bottom(const indigo_app *app, const indigo_input *input, indigo_canvas *c)
{
    (void) input;
    indigo_palette pal = indigo_layout_palette(&app->settings);

    indigo_canvas_init(c, INDIGO_BOTTOM_WIDTH, INDIGO_BOTTOM_HEIGHT);
    indigo_canvas_rect(c, 0, 0, INDIGO_BOTTOM_WIDTH, INDIGO_BOTTOM_HEIGHT, pal.bg_bottom);
    indigo_canvas_rect(c, 0, 0, INDIGO_BOTTOM_WIDTH, 42, pal.bar);
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
    case INDIGO_SCREEN_SETTINGS:
        build_bottom_settings(app, c);
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
