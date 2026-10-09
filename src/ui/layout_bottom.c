/* The bottom (touch) screen of each app screen. */

#include "ui/layout_internal.h"

static void
build_bottom_signin(const indigo_app *app, indigo_canvas *c)
{
    const indigo_signin *s = &app->signin;
    indigo_rect b = indigo_layout_button_rect(INDIGO_ACTION_SIGN_IN);

    indigo_canvas_text(c, 14, 8, 0.75f, COL_TEXT, "Sign in");
    indigo_layout_field_row(c, INDIGO_ACTION_FIELD_SERVICE, s, INDIGO_FIELD_SERVICE);
    indigo_layout_field_row(c, INDIGO_ACTION_FIELD_HANDLE, s, INDIGO_FIELD_HANDLE);
    indigo_layout_field_row(c, INDIGO_ACTION_FIELD_PASSWORD, s, INDIGO_FIELD_PASSWORD);

    indigo_canvas_rect(c, b.x, b.y, b.w, b.h,
                       indigo_signin_ready(s) && s->phase == INDIGO_PHASE_IDLE
                           ? COL_PILL_ACTIVE
                           : COL_PILL);
    indigo_canvas_text(c, b.x + 100, b.y + 8, 0.7f, COL_TEXT, "%s",
                       s->phase == INDIGO_PHASE_BUSY ? "Signing in..." : "Sign in");
}

static void
build_bottom_posts(const indigo_app *app, indigo_canvas *c)
{
    bool thread = app->screen == INDIGO_SCREEN_THREAD;
    const indigo_timeline *t = thread ? &app->thread : &app->timeline;
    const indigo_post *sel = indigo_timeline_selected(t);

    if (thread && app->confirm_delete) {
        indigo_canvas_text(c, 14, 8, 0.75f, COL_TEXT, "Delete post?");
        indigo_layout_back_button(c, INDIGO_ACTION_BACK, "Cancel");
        indigo_canvas_text(c, 14, 74, 0.7f, COL_TEXT, "This cannot be undone.");
        indigo_layout_action_pill(c, INDIGO_ACTION_DELETE_CONFIRM, false, false,
                                  COL_PILL_ACTIVE, "A Delete");
        indigo_layout_action_pill(c, INDIGO_ACTION_DELETE_CANCEL, false, false,
                                  COL_PILL_ACTIVE, "B Cancel");
        return;
    }

    indigo_canvas_text(c, 14, 8, 0.75f, COL_TEXT, thread ? "Thread" : "Timeline");
    if (!indigo_layout_image_button(c, app)) {
        indigo_canvas_text(c, 118, 14, 0.5f, t->status_is_error ? COL_ERROR : COL_TEXT_DIM,
                           "%.22s", t->loading ? "Loading..." : t->status);
    }
    /* The button hints live in the top screen's title bar; a bottom pill is
     * labelled with its action so no back hint appears twice. A feed view's B
     * goes back to the picker, so its button is named for the picker. */
    indigo_layout_back_button(c, thread ? INDIGO_ACTION_BACK : INDIGO_ACTION_MENU,
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
                 indigo_layout_author_name(p));
        indigo_layout_list_row(c, (indigo_action) (INDIGO_ACTION_ROW0 + row), idx == t->selected, title,
                 p->text, p->avatar);
    }

    indigo_layout_action_pill(c, INDIGO_ACTION_LIKE, sel && sel->like_uri[0], sel && sel->like_pending,
                COL_PILL_ACTIVE, sel && sel->like_uri[0] ? "Y Liked" : "Y Like");
    indigo_layout_action_pill(c, INDIGO_ACTION_REPOST, sel && sel->repost_uri[0], sel && sel->repost_pending,
                COL_PILL_ACTIVE, sel && sel->repost_uri[0] ? "X Reposted" : "X Repost");
    if (thread) {
        indigo_layout_action_pill(c, INDIGO_ACTION_REPLY, false, false, COL_PILL_ACTIVE, "A Reply");
        if (indigo_app_selected_post_is_own(app)) {
            indigo_layout_action_pill(c, INDIGO_ACTION_DELETE, false, false, COL_PILL_ACTIVE, "Delete");
        } else {
            indigo_layout_action_pill(c, INDIGO_ACTION_AUTHOR, false, false, COL_PILL_ACTIVE, "Profile");
        }
    } else {
        indigo_layout_action_pill(c, INDIGO_ACTION_OPEN, false, false, COL_PILL_ACTIVE, "A Open");
        indigo_layout_action_pill(c, INDIGO_ACTION_REFRESH, t->loading, false, COL_PILL_ACTIVE, "Reload");
    }
}

static void
build_bottom_profile(const indigo_app *app, indigo_canvas *c)
{
    const indigo_profile *p = &app->profile;
    bool ready = p->loaded && !p->loading;
    indigo_rect fb = indigo_layout_button_rect(INDIGO_ACTION_FOLLOW);

    indigo_canvas_text(c, 14, 8, 0.75f, COL_TEXT, "Profile");
    indigo_layout_back_button(c, INDIGO_ACTION_BACK, "Back");

    /* Greyed until the profile has loaded: follow, mute and block all address
     * the subject by did, which only the profile response carries. */
    indigo_canvas_rect(c, fb.x, fb.y, fb.w,
                       fb.h,
                       ready && !p->follow_busy ? COL_PILL_ACTIVE : COL_PILL);
    indigo_canvas_text(c, fb.x + 100, fb.y + 12, 0.7f,
                       ready && !p->follow_busy ? COL_TEXT : COL_TEXT_DIM, "%s",
                       !p->loaded           ? "Loading..."
                       : p->follow_busy     ? (p->following ? "Following..." : "Unfollowing...")
                       : p->following       ? "Following"
                                             : "Follow");

    indigo_layout_toggle_button(c, INDIGO_ACTION_MUTE, ready, p->mute_busy, "Unmute", "Mute");
    indigo_layout_toggle_button(c, INDIGO_ACTION_BLOCK, ready, p->block_busy, "Unblock", "Block");

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
        indigo_layout_action_pill(c, INDIGO_ACTION_PINNED, p->pinned_uri[0] != '\0', false, COL_PILL_ACTIVE,
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
    indigo_layout_back_button(c, INDIGO_ACTION_BACK, "Back");
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
        indigo_layout_list_row(c, (indigo_action) (INDIGO_ACTION_ROW0 + row), idx == n->selected, title,
                 it->text[0] ? it->text : indigo_layout_note_verb(it->kind), it->avatar);
    }
    indigo_layout_action_pill(c, INDIGO_ACTION_OPEN, false, false, COL_PILL_ACTIVE, "A Open");
    indigo_layout_action_pill(c, INDIGO_ACTION_REFRESH, n->loading, false, COL_PILL_ACTIVE, "Reload");
}

static void
build_bottom_menu(const indigo_app *app, indigo_canvas *c)
{
    char position[32];

    if (app->menu.showing_link) {
        indigo_line lines[5];
        int truncated = 0;
        unsigned n;
        indigo_canvas_text(c, 14, 8, 0.75f, COL_TEXT, "Link address");
        indigo_layout_back_button(c, INDIGO_ACTION_BACK, "Back");
        n = indigo_wrap(app->menu.link_url, 48, lines, 4, &truncated);
        for (unsigned i = 0; i < n; i++) {
            indigo_canvas_text(c, 14, 52 + 23.0f * (float) i, 0.55f, COL_TEXT,
                               "%.*s%s", (int) lines[i].len,
                               app->menu.link_url + lines[i].start,
                               truncated && i + 1 == n ? "..." : "");
        }
        if (app->menu.qr_size == 0) {
            indigo_canvas_text(c, 14, 170, 0.55f, COL_TEXT_DIM, "QR unavailable for this URL.");
        }
        return;
    }
    indigo_canvas_text(c, 14, 8, 0.75f, COL_TEXT, "%s", app->menu.title);
    indigo_layout_back_button(c, INDIGO_ACTION_BACK, "Close");

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
    indigo_rect a = indigo_layout_button_rect(INDIGO_ACTION_ATTACH);
    indigo_rect t = indigo_layout_button_rect(INDIGO_ACTION_TOGGLE);
    indigo_rect s = indigo_layout_button_rect(INDIGO_ACTION_SEND);
    bool can_toggle = indigo_compose_can_toggle(d);

    indigo_canvas_text(c, 14, 8, 0.75f, COL_TEXT, "%s", indigo_layout_compose_title(d));
    indigo_layout_back_button(c, INDIGO_ACTION_BACK, "Back");

    indigo_canvas_rect(c, e.x, e.y, e.w, e.h, COL_PILL);
    if (d->text[0]) {
        indigo_line lines[2];
        int cut;
        unsigned units = (unsigned) ((e.w - 20) / (INDIGO_CHAR_WIDTH * 0.6f));
        unsigned n = indigo_wrap(d->text, units, lines, 2, &cut);

        for (unsigned i = 0; i < n; i++) {
            indigo_canvas_text(c, e.x + 10, e.y + 8 + 22 * (float) i, 0.6f, COL_TEXT, "%.*s%s",
                               (int) lines[i].len, d->text + lines[i].start,
                               cut && i + 1 == n ? "..." : "");
        }
    } else {
        indigo_canvas_text(c, e.x + 10, e.y + 30, 0.7f, COL_TEXT_DIM, "Tap to write");
    }

    /* One picture can be attached; the same control takes it off again. */
    indigo_canvas_rect(c, a.x, a.y, a.w, a.h, indigo_compose_has_image(d) ? COL_PILL_ACTIVE : COL_PILL);
    if (indigo_compose_has_image(d)) {
        indigo_canvas_text(c, a.x + 14, a.y + 7, 0.6f, COL_TEXT, "Remove the image - Select");
    } else {
        indigo_canvas_text(c, a.x + 14, a.y + 7, 0.6f, COL_TEXT, "Add an image - Select");
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
        if (s->kind == INDIGO_SEARCH_LIKED_BY || s->kind == INDIGO_SEARCH_REPOSTED_BY) {
            indigo_canvas_text(c, q.x + 8, q.y + 8, 0.55f, COL_TEXT_SOFT, "%s",
                               s->kind == INDIGO_SEARCH_LIKED_BY
                                   ? "People who liked this post"
                                   : "People who reposted this post");
        } else if (s->kind == INDIGO_SEARCH_AUTHOR) {
          /* The box doubles as the tab switch: it names the person and what
           * a tap on it will show instead. */
          indigo_canvas_text(
              c, q.x + 8, q.y + 8, 0.55f, COL_TEXT_SOFT, "@%.26s  Tap: %s",
              s->subject,
              wf_profile_tab_name(indigo_app_author_tab_after(app)));
        } else {
          indigo_canvas_text(c, q.x + 8, q.y + 8, 0.55f, COL_TEXT_SOFT,
                             "@%.40s", s->subject);
        }
    }
    /* The search screen has no status line in its header to give up, so the
     * button simply lands in the empty middle of the bar. */
    indigo_layout_image_button(c, app);
    indigo_layout_back_button(c, INDIGO_ACTION_BACK, "Back");

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
            indigo_layout_list_row(c, (indigo_action) (INDIGO_ACTION_ROW0 + row), idx == s->selected, title,
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
            indigo_layout_list_row(c, (indigo_action) (INDIGO_ACTION_ROW0 + row), idx == s->selected, title,
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
            indigo_layout_list_row(c, (indigo_action) (INDIGO_ACTION_ROW0 + row), idx == s->selected, title,
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
    indigo_layout_action_pill(c, INDIGO_ACTION_AUTHOR,
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
    indigo_layout_back_button(c, INDIGO_ACTION_BACK, "Back");

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

/* The update screen, bottom screen: Back and the one button, whose label
 * follows the state. No button is drawn while there is nothing to press, so a
 * touch during a download is not a press of something that looks available. */
static void
build_bottom_update(const indigo_app *app, indigo_canvas *c)
{
    char label[64];

    indigo_canvas_text(c, 14, 10, 0.9f, COL_TEXT, "Update");
    indigo_layout_back_button(c, INDIGO_ACTION_BACK, "Back");
    indigo_updater_button_label(&app->updater, label, sizeof label);
    if (label[0]) {
        indigo_rect r = indigo_layout_button_rect(INDIGO_ACTION_UPDATE);

        indigo_canvas_rect(c, r.x, r.y, r.w, r.h, COL_PILL_ACTIVE);
        indigo_canvas_text(c, r.x + 12, r.y + 11, 0.7f, COL_TEXT, "A  %s", label);
    }
    indigo_layout_paragraph(c, 14, 60, (float) INDIGO_BOTTOM_WIDTH, 0.55f, COL_TEXT_SOFT, 4,
              "Indigo replaces its own .3dsx, and only when you ask. If anything goes wrong "
              "the old build is kept.");
}

/* The viewer, bottom screen: the controls, and the description of the picture
 * rather than a caption over it. The top screen is where the picture is, and
 * text on top of a photograph reads as part of the photograph; here there is
 * room for the whole sentence. */
static void
build_bottom_image(const indigo_app *app, indigo_canvas *c)
{
    const indigo_image *img = &app->image;
    float y = 62.0f;

    indigo_canvas_text(c, 14, 8, 0.75f, COL_TEXT, "Image");
    indigo_layout_back_button(c, INDIGO_ACTION_BACK, "Close");

    if (img->alt[0] && app->settings.alt_text) {
        y = indigo_layout_paragraph(c, 14, y, (float) INDIGO_BOTTOM_WIDTH, 0.55f, COL_TEXT_SOFT, 9,
                      img->alt);
        y += 6.0f;
    } else if (!img->alt[0]) {
        /* Said rather than left blank: a picture with no description is the
         * normal case, and silence would read as a description that failed to
         * arrive. */
        indigo_canvas_text(c, 14, y, 0.55f, COL_TEXT_DIM, "The author added no description.");
        y += 20.0f;
    }
    /* Bluesky allows four images and a post carries one URL, so the viewer can
     * only ever show the first. Saying so is the difference between one picture
     * of four and a post that happens to have one picture. */
    if (img->count > 1) {
        indigo_canvas_text(c, 14, y, 0.55f, COL_TEXT_DIM, "1 of %u images.", img->count);
        y += 20.0f;
    }
    if (y + 26.0f > (float) INDIGO_BOTTOM_HEIGHT) {
        indigo_canvas_text(c, 14, (float) INDIGO_BOTTOM_HEIGHT - 14.0f, 0.55f, COL_TEXT_DIM,
                           "...");
    }
}

void
indigo_layout_build_bottom(const indigo_app *app, const indigo_input *input, indigo_canvas *c)
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
    case INDIGO_SCREEN_IMAGE:
        build_bottom_image(app, c);
        break;
    case INDIGO_SCREEN_UPDATE:
        build_bottom_update(app, c);
        break;
    }
}
