/* The top screen of each app screen. */

#include "ui/layout_internal.h"

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

static void
build_top_profile(const indigo_app *app, indigo_canvas *c)
{
    const indigo_profile *p = &app->profile;

    indigo_layout_top_title(c, "Profile", "Y  Follow   X  Mute   R  Block");
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
    indigo_layout_draw_avatar(c, p->avatar, 14, 38, PROFILE_AVATAR);
    /* The display name is cut to 30 characters because a 40px avatar plus the
     * longest name that still fits is narrower than the full 400px line. */
    indigo_canvas_text(c, 64, 44, 0.85f, COL_TEXT, "%.26s",
                       p->display_name[0] ? p->display_name : p->handle);
    indigo_canvas_text(c, 64, 72, 0.6f, COL_TEXT_DIM, "@%s%s", p->handle,
                       p->following ? "   Following" : "");
    indigo_layout_top_paragraph(c, 18, 98, 0.6f, COL_TEXT_SOFT, 4, p->bio);
    indigo_canvas_text(c, 18, 196, 0.6f, COL_TEXT, "%u posts", p->posts);
    indigo_canvas_text(c, 128, 196, 0.6f, COL_TEXT, "%u followers", p->followers);
    indigo_canvas_text(c, 262, 196, 0.6f, COL_TEXT, "%u following", p->follows);
}

static void
build_top_notifications(const indigo_app *app, indigo_canvas *c)
{
    const indigo_notifications *n = &app->notifications;
    const indigo_notification *sel = indigo_notifications_selected(n);

    indigo_layout_top_title(c, "Notifications", "B  Back   SEL  Reload");
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
    indigo_layout_draw_avatar(c, sel->avatar, 12, 46, HEAD_AVATAR);
    indigo_canvas_text(c, 44, 48, 0.75f, COL_TEXT, "%.28s", sel->name[0] ? sel->name : sel->handle);
    indigo_canvas_text(c, 44, 74, 0.6f, COL_TEXT_DIM, "@%s", sel->handle);
    indigo_canvas_text(c, 18, 96, 0.65f, sel->unread ? COL_LINK : COL_TEXT_SOFT, "%s%s",
                       indigo_layout_note_verb(sel->kind), sel->unread ? "  (new)" : "");
    if (sel->text[0]) {
        indigo_layout_top_paragraph(c, 18, 124, 0.6f, COL_TEXT, 4, sel->text);
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
    indigo_layout_top_title(c, indigo_search_title(s),
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
            indigo_layout_top_paragraph(c, 18, 82, 0.6f, COL_TEXT_SOFT, 3, lsel->description);
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
                           indigo_layout_author_name(psel));
        indigo_canvas_text(c, 18, 76, 0.55f, COL_TEXT_DIM, "@%s", psel->handle);
        indigo_layout_draw_post_body(c, psel, app->settings.alt_text, app->settings.text_scale);
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
    char note[200];

    if (app->menu.picking_image) {
        indigo_layout_top_title(c, "Add image", "B  Close");
        indigo_canvas_text(c, 18, 56, 0.6f, COL_TEXT_SOFT, "Choose a picture from");
        indigo_canvas_text(c, 18, 78, 0.6f, COL_TEXT, "%s", INDIGO_IMAGES_DIR);
        indigo_canvas_text(c, 18, 100, 0.6f, COL_TEXT, "and %s (camera)", INDIGO_CAMERA_DIR);
        snprintf(note, sizeof note,
                 "Copy .jpg or .png files there from a computer; each must be under %d KB. "
                 "You can describe the picture for people who cannot see it after choosing it.",
                 WF_ATTACH_MAX_BYTES / 1000);
        indigo_layout_top_paragraph(c, 18, 124, 0.55f, COL_TEXT_DIM, 4, note);
        return;
    }
    indigo_layout_top_title(c, "Menu", "B  Close");
    indigo_canvas_text(c, 18, 64, 0.7f, COL_TEXT_SOFT, "Signed in as");
    indigo_canvas_text(c, 18, 90, 0.8f, COL_TEXT, "%s", app->signin.account);
    indigo_canvas_text(c, 18, 150, 0.6f, COL_TEXT_DIM, "Wolfram: %s",
                       app->wolfram_linked ? "linked" : "not linked");
    indigo_canvas_text(c, 18, 208, 0.6f, COL_TEXT_DIM, "START  Exit");
}

static void
build_top_compose(const indigo_app *app, indigo_canvas *c)
{
    const indigo_compose *d = &app->compose;
    float y = 44;

    indigo_layout_top_title(c, indigo_layout_compose_title(d), "A  Write   B  Back");
    if (d->has_target) {
        indigo_canvas_text(c, 18, y, 0.55f, COL_TEXT_DIM, "%s @%s",
                           d->mode == INDIGO_COMPOSE_QUOTE ? "Quoting" : "Replying to",
                           d->target.handle);
        y = indigo_layout_top_paragraph(c, 18, y + 18, 0.55f, COL_TEXT_DIM, 2, d->target.text) + 8;
    }
    if (d->text[0]) {
        indigo_layout_top_paragraph(c, 18, y + 4, 0.65f, COL_TEXT, 5, d->text);
    } else {
        indigo_canvas_text(c, 18, y + 4, 0.65f, COL_TEXT_DIM, "Tap the box below to write.");
    }
    indigo_canvas_text(c, 18, 214, 0.55f, indigo_layout_utf8_length(d->text) > 300 ? COL_ERROR : COL_TEXT_DIM,
                       "%u / 300", indigo_layout_utf8_length(d->text));
    /* The pill on the bottom screen is the control; this is the reading of it,
     * where there is room to spell the rule out. */
    if (indigo_compose_can_gate(d)) {
        indigo_canvas_text(c, 18, 188, 0.6f, COL_TEXT_DIM, "Replies: %s",
                           indigo_compose_gate_label(d));
    }
    if (indigo_compose_has_image(d)) {
        indigo_canvas_text(c, 18, 170, 0.6f, COL_TEXT_DIM, "Image: %.30s%s", indigo_compose_image_name(d),
                           d->image_alt[0] ? " (alt text set)" : " (no alt text)");
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
    indigo_layout_top_title(c, "Settings", "");

    const char *theme_str =
        s->theme == INDIGO_THEME_LIGHT ? "Light"
        : s->theme == INDIGO_THEME_DARK ? "Dark"
                                        : "Auto (follows system)";
    indigo_canvas_text(c, 18, 44, 0.65f, COL_TEXT, "Theme: %s", theme_str);
    indigo_canvas_text(c, 18, 66, 0.65f, COL_TEXT, "Text scale: %u%%", s->text_scale);
    indigo_canvas_text(c, 18, 88, 0.65f, COL_TEXT, "Reduce motion (reserved): %s",
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

/* The update screen, top screen: which build this is, and what the updater
 * knows. Every state says in plain words what happens next, and the one that
 * offers an update says what it is checked against and what that does not
 * prove, because "verified" is a word that should not outrun the check. */
static void
build_top_update(const indigo_app *app, indigo_canvas *c)
{
    const indigo_updater *u = &app->updater;
    const char *state;
    uint32_t colour = COL_TEXT_SOFT;
    float y;

    indigo_layout_top_title(c, "Update", "");
    indigo_canvas_text(c, 18, 44, 0.65f, COL_TEXT, "This build: %s",
                       u->current[0] ? u->current : "unknown");
    indigo_canvas_text(c, 18, 62, 0.5f, COL_TEXT_DIM, "%s (build %d, %s)", INDIGO_BUILD_COMMIT,
                       INDIGO_BUILD_NUMBER, INDIGO_BUILD_DATE);

    switch (u->state) {
    case INDIGO_UPDATER_UNAVAILABLE:
        state = u->message;
        break;
    case INDIGO_UPDATER_IDLE:
        state = "I have not asked GitHub yet. Updates come only from the releases of ewanc26/indigo.";
        break;
    case INDIGO_UPDATER_CHECKING:
        state = "Asking GitHub for the latest release...";
        break;
    case INDIGO_UPDATER_UP_TO_DATE:
        state = "You have the latest release.";
        break;
    case INDIGO_UPDATER_AVAILABLE:
        state = "A newer release is available. Nothing has been downloaded.";
        break;
    case INDIGO_UPDATER_DOWNLOADING:
        state = "Downloading, then checking the size and SHA-256 before anything on the card changes...";
        break;
    case INDIGO_UPDATER_READY:
        state = "Downloaded and checked. Putting it in place...";
        break;
    case INDIGO_UPDATER_INSTALLED:
        state = "Done. Press START, then open Indigo again from the Homebrew Menu.";
        break;
    case INDIGO_UPDATER_FAILED:
    default:
        state = u->message;
        colour = COL_ERROR;
        break;
    }
    y = indigo_layout_top_paragraph(c, 18, 90, 0.6f, colour, 4, state);
    if (u->state == INDIGO_UPDATER_AVAILABLE) {
        indigo_canvas_text(c, 18, y + 6, 0.7f, COL_TEXT, "Available: %s (%lu KB)", u->latest,
                           (u->size + 1023) / 1024);
        indigo_layout_top_paragraph(c, 18, y + 30, 0.5f, COL_TEXT_DIM, 4,
                      "The check is a SHA-256 published in the same release. It catches a bad "
                      "download; it does not prove the release is mine, because it is not signed.");
    } else if (u->state == INDIGO_UPDATER_DOWNLOADING || u->state == INDIGO_UPDATER_READY ||
               u->state == INDIGO_UPDATER_INSTALLED) {
        indigo_canvas_text(c, 18, y + 6, 0.55f, COL_TEXT_DIM,
                           "The old build is kept as indigo-previous.3dsx until the new one starts.");
    }
}

/* The full-size viewer, top screen: the picture and nothing else.
 *
 * The surround is a fixed near-black rather than the theme's background, which
 * is the one place in Indigo that ignores the palette. A photograph is judged
 * against what is around it, and a pale surround puts a box round every image
 * that is lighter than the box; every other screen is showing text and panels,
 * where the theme is the point. */
static void
build_top_image(const indigo_app *app, indigo_canvas *c)
{
    const indigo_image *img = &app->image;
    /* A square is the assumption that distorts least when the server declared
     * no aspect ratio, because the box drawn is stretched to whatever shape it
     * is given -- the same choice the detail band makes. */
    float aw = img->aspect_w && img->aspect_h ? (float) img->aspect_w : 1.0f;
    float ah = img->aspect_w && img->aspect_h ? (float) img->aspect_h : 1.0f;
    float k = INDIGO_TOP_WIDTH / aw;
    float by_height = INDIGO_TOP_HEIGHT / ah;
    float w;
    float h;

    indigo_canvas_rect(c, 0, 0, INDIGO_TOP_WIDTH, INDIGO_TOP_HEIGHT, COL_VIEWER_BG);
    if (!img->url[0]) {
        indigo_canvas_text(c, 18, 104, 0.7f, COL_TEXT_SOFT, "No image to show.");
        return;
    }
    /* The detail band's fit, against the whole screen rather than the band:
     * scale by the smaller of the two ratios, so a 1:3 panorama comes out
     * 400x133 instead of being 1200px tall. */
    if (by_height < k) {
        k = by_height;
    }
    w = aw * k;
    h = ah * k;
    indigo_canvas_image(c, (INDIGO_TOP_WIDTH - w) / 2.0f, (INDIGO_TOP_HEIGHT - h) / 2.0f, w,
                        h, img->url, indigo_media_placeholder_color(img->url));
}

void
indigo_layout_build_top(const indigo_app *app, indigo_canvas *c)
{
    indigo_palette pal = indigo_layout_palette(&app->settings);

    indigo_canvas_init(c, INDIGO_TOP_WIDTH, INDIGO_TOP_HEIGHT);
    /* The viewer paints the whole screen itself, in a colour that is not the
     * theme's, so the themed background is not drawn first and then covered. */
    if (app->screen != INDIGO_SCREEN_IMAGE) {
        indigo_canvas_rect(c, 0, 0, INDIGO_TOP_WIDTH, INDIGO_TOP_HEIGHT, pal.bg_top);
    }

    if (app->screen == INDIGO_SCREEN_HOME || app->screen == INDIGO_SCREEN_THREAD) {
        indigo_canvas_rect(c, 0, 0, INDIGO_TOP_WIDTH, 32, pal.bar);
        indigo_layout_build_top_post(app, c);
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
    case INDIGO_SCREEN_IMAGE:
        build_top_image(app, c);
        return;
    case INDIGO_SCREEN_UPDATE:
        build_top_update(app, c);
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
