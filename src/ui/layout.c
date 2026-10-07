#include "ui/layout_internal.h"

static const indigo_rect s_pill[4] = {
    {4, PILL_Y, PILL_W, PILL_H},
    {84, PILL_Y, PILL_W, PILL_H},
    {164, PILL_Y, PILL_W, PILL_H},
    {244, PILL_Y, PILL_W, PILL_H},
};

/* Top-right of the bottom screen's header bar: Back, or Menu on Home. */
static const indigo_rect s_back_button = {232, 4, 82, 34};

/* Opens the full-size image viewer, in the header bar's empty middle. It is
 * drawn only when the selected post has an image to open, and it takes the
 * status text's slot to do it: a status is only ever a transient "Loading...",
 * a "Nothing here yet" for a list with no posts, or a fetch error, and the
 * first two cannot happen while a post is selected. So when this button is up,
 * the only status it can displace is one worth more of the room than the
 * button -- which is why that status moves to the top screen's title bar
 * instead of being dropped. */
static const indigo_rect s_image_button = {104, 4, 92, 34};


/* Compose: the draft box, the image attach control, the Reply/Quote switch and
 * Post, 8px apart so a thumb never lands on two. */
static const indigo_rect s_edit_button = {14, 52, 292, 56};
static const indigo_rect s_attach_button = {14, 116, 292, 32};
static const indigo_rect s_toggle_button = {14, 156, 292, 32};
static const indigo_rect s_send_button = {14, 196, 292, 38};

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
    case INDIGO_ACTION_IMAGE:
        return s_image_button;
    case INDIGO_ACTION_UPDATE:
        return (indigo_rect) {SETTINGS_ROW_X, 150, SETTINGS_ROW_W, 40};
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
    case INDIGO_ACTION_ATTACH:
        return s_attach_button;
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

indigo_palette
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
        INDIGO_ACTION_REPOST, INDIGO_ACTION_OPEN, INDIGO_ACTION_REFRESH, INDIGO_ACTION_MENU,
        INDIGO_ACTION_IMAGE};
    static const indigo_action thread_actions[] = {
        INDIGO_ACTION_ROW0, INDIGO_ACTION_ROW1, INDIGO_ACTION_ROW2, INDIGO_ACTION_LIKE,
        INDIGO_ACTION_REPOST, INDIGO_ACTION_REPLY, INDIGO_ACTION_AUTHOR, INDIGO_ACTION_BACK,
        INDIGO_ACTION_IMAGE};
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
        INDIGO_ACTION_EDIT, INDIGO_ACTION_ATTACH, INDIGO_ACTION_TOGGLE, INDIGO_ACTION_SEND,
        INDIGO_ACTION_BACK};
    static const indigo_action search_actions[] = {
        INDIGO_ACTION_FIELD_QUERY, INDIGO_ACTION_ROW0, INDIGO_ACTION_ROW1,
        INDIGO_ACTION_ROW2, INDIGO_ACTION_AUTHOR, INDIGO_ACTION_BACK, INDIGO_ACTION_IMAGE};
    /* The viewer has nothing to choose between: it is one picture, and the way
     * out is the same way every screen has one. */
    static const indigo_action image_actions[] = {INDIGO_ACTION_BACK};
    static const indigo_action settings_actions[] = {
        INDIGO_ACTION_SETTINGS_ROW0, INDIGO_ACTION_SETTINGS_ROW1,
        INDIGO_ACTION_SETTINGS_ROW2, INDIGO_ACTION_SETTINGS_ROW3,
        INDIGO_ACTION_SETTINGS_ROW4, INDIGO_ACTION_SETTINGS_ROW5,
        INDIGO_ACTION_SETTINGS_ROW6, INDIGO_ACTION_SETTINGS_ROW7,
        INDIGO_ACTION_BACK};
    static const indigo_action update_actions[] = {INDIGO_ACTION_UPDATE, INDIGO_ACTION_BACK};
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
    case INDIGO_SCREEN_IMAGE:
        USE(image_actions);
        break;
    case INDIGO_SCREEN_UPDATE:
        USE(update_actions);
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
void
indigo_layout_build(const indigo_app *app, const indigo_input *input,
                    indigo_canvas *top, indigo_canvas *bottom)
{
    indigo_layout_build_top(app, top);
    indigo_layout_build_bottom(app, input, bottom);
}
