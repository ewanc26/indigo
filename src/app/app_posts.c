/* Input on the screens that show posts: the home timeline, a thread, a profile and notifications. */

#include "app/app_internal.h"

/* The touch actions the timeline and the thread share: pick a row, like,
 * repost, open the picture. */
static void
post_list_touch(indigo_app *app, indigo_timeline *t, indigo_action a)
{
    switch (a) {
    case INDIGO_ACTION_ROW0:
    case INDIGO_ACTION_ROW1:
    case INDIGO_ACTION_ROW2:
        indigo_timeline_select(t, t->scroll + (unsigned) (a - INDIGO_ACTION_ROW0),
                               INDIGO_TIMELINE_ROWS);
        break;
    case INDIGO_ACTION_LIKE:
        indigo_app_toggle_like(app);
        break;
    case INDIGO_ACTION_REPOST:
        indigo_app_toggle_repost(app);
        break;
    case INDIGO_ACTION_IMAGE:
        indigo_app_open_image(app);
        break;
    default:
        break;
    }
}

/* Navigation and like/repost shared by the timeline and thread lists. */
/* The rows a touch drag has moved a list by this frame: positive when the finger
 * went up, so later rows come into view. Only a drag that began on one of the
 * list's rows counts, so dragging from a button or the header never scrolls. */
int
indigo_app_drag_rows(const indigo_app *app, const indigo_input *input)
{
    indigo_action a;

    if (input->drag_rows == 0) {
        return 0;
    }
    a = indigo_layout_hit_app(app, input->drag_start_x, input->drag_start_y);
    if (a < INDIGO_ACTION_ROW0 || a > INDIGO_ACTION_ROW2) {
        return 0;
    }
    return input->drag_rows;
}

static void
update_list(indigo_app *app, const indigo_input *input, indigo_timeline *t)
{
    int drag = indigo_app_drag_rows(app, input);

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
    if (drag != 0) {
        indigo_timeline_move(t, drag, INDIGO_TIMELINE_ROWS);
    }
    if (input->like) {
        indigo_app_toggle_like(app);
    }
    if (input->repost) {
        indigo_app_toggle_repost(app);
    }
}

/* A feed view's B returns to the picker it came from, and the home screen goes
 * back to the Following timeline. */
static void
leave_feed(indigo_app *app)
{
    app->feed_uri[0] = '\0';
    app->feed_name[0] = '\0';
    indigo_app_go_back(app);
    indigo_timeline_begin_fetch(&app->timeline, true);
    app->request = INDIGO_REQUEST_TIMELINE_REFRESH;
}

void
indigo_app_update_home(indigo_app *app, const indigo_input *input)
{
    indigo_timeline *t = &app->timeline;
    const indigo_post *sel = indigo_timeline_selected(t);

    update_list(app, input, t);
    if (input->confirm && sel) {
        indigo_app_open_thread(app, sel->uri);
    }
    if (input->back) {
        if (app->feed_uri[0]) {
            leave_feed(app);
        } else {
            indigo_app_open_menu(app);
        }
    }
    if (input->refresh) {
        indigo_app_refresh_timeline(app);
    }
    /* ZR as well as the button: every button here is on the touchscreen, but a
     * New 3DS has two hands on the shell and no touchscreen in the right one. */
    if (input->zr && indigo_app_open_image(app)) {
        return;
    }
    if (input->touch_pressed) {
        indigo_action a = indigo_layout_hit_app(app, input->touch_x, input->touch_y);

        switch (a) {
        case INDIGO_ACTION_ROW0:
        case INDIGO_ACTION_ROW1:
        case INDIGO_ACTION_ROW2:
        case INDIGO_ACTION_LIKE:
        case INDIGO_ACTION_REPOST:
        case INDIGO_ACTION_IMAGE:
            post_list_touch(app, t, a);
            break;
        case INDIGO_ACTION_OPEN:
            if (sel) {
                indigo_app_open_thread(app, sel->uri);
            }
            break;
        case INDIGO_ACTION_REFRESH:
            indigo_app_refresh_timeline(app);
            break;
        case INDIGO_ACTION_MENU:
            /* The bottom-right button does what B does on this screen. */
            if (app->feed_uri[0]) {
                leave_feed(app);
            } else {
                indigo_app_open_menu(app);
            }
            break;
        default:
            break;
        }
    }
    if (app->request == INDIGO_REQUEST_NONE && indigo_timeline_wants_page(t)) {
        indigo_timeline_begin_fetch(t, false);
        if (app->feed_uri[0]) {
            app->request = INDIGO_REQUEST_FEED;
        } else {
            app->request = INDIGO_REQUEST_TIMELINE_MORE;
        }
    }
}

void
indigo_app_update_thread(indigo_app *app, const indigo_input *input)
{
    indigo_timeline *t = &app->thread;
    const indigo_post *sel = indigo_timeline_selected(t);

    update_list(app, input, t);
    if (input->back) {
        indigo_app_go_back(app);
        return;
    }
    if (input->confirm && sel) {
        indigo_app_begin_compose(app, INDIGO_COMPOSE_REPLY, sel);
    }
    if (input->refresh && sel) {
        indigo_app_open_profile(app, sel->handle);
    }
    if (input->zr && indigo_app_open_image(app)) {
        return;
    }
    if (input->touch_pressed) {
        indigo_action a = indigo_layout_hit_app(app, input->touch_x, input->touch_y);

        switch (a) {
        case INDIGO_ACTION_ROW0:
        case INDIGO_ACTION_ROW1:
        case INDIGO_ACTION_ROW2:
        case INDIGO_ACTION_LIKE:
        case INDIGO_ACTION_REPOST:
        case INDIGO_ACTION_IMAGE:
            post_list_touch(app, t, a);
            break;
        case INDIGO_ACTION_REPLY:
            if (sel) {
                indigo_app_begin_compose(app, INDIGO_COMPOSE_REPLY, sel);
            }
            break;
        case INDIGO_ACTION_AUTHOR:
            if (sel) {
                indigo_app_open_profile(app, sel->handle);
            }
            break;
        case INDIGO_ACTION_BACK:
            indigo_app_go_back(app);
            break;
        default:
            break;
        }
    }
}

void
indigo_app_update_profile(indigo_app *app, const indigo_input *input)
{
    indigo_profile *p = &app->profile;

    if (input->back) {
        indigo_app_go_back(app);
    }
    /* Y follows, matching the post screen's use of Y for the primary action
     * on the focused item. The button shows the state, so the same press
     * unfollows once you are following. */
    if ((input->confirm || input->like) && p->loaded && !p->loading) {
        indigo_app_toggle_follow(app);
    }
    /* X is repost on the post screen, but this screen has no posts, so it
     * takes the moderation actions instead. */
    if (input->repost && p->loaded && !p->loading) {
        indigo_app_toggle_mute(app);
    }
    if (input->page_down && p->loaded && !p->loading) {
        indigo_app_toggle_block(app);
    }
    if (input->touch_pressed) {
        switch (indigo_layout_hit_app(app, input->touch_x, input->touch_y)) {
        case INDIGO_ACTION_FOLLOW:
            indigo_app_toggle_follow(app);
            break;
        case INDIGO_ACTION_MUTE:
            indigo_app_toggle_mute(app);
            break;
        case INDIGO_ACTION_BLOCK:
            indigo_app_toggle_block(app);
            break;
        case INDIGO_ACTION_FOLLOWERS:
            indigo_app_open_people(app, INDIGO_SEARCH_FOLLOWERS, p->handle);
            break;
        case INDIGO_ACTION_FOLLOWING:
            indigo_app_open_people(app, INDIGO_SEARCH_FOLLOWING, p->handle);
            break;
        case INDIGO_ACTION_POSTS:
            indigo_app_open_author_posts(app, p->handle);
            break;
        case INDIGO_ACTION_PINNED:
            /* Guarded here as well as by the disabled button: the action is
             * always in the profile's hit list, so a touch on the blank spot
             * where the pill would be still lands here. */
            if (p->pinned_uri[0]) {
                indigo_app_open_thread(app, p->pinned_uri);
            }
            break;
        case INDIGO_ACTION_BACK:
            indigo_app_go_back(app);
            break;
        default:
            break;
        }
    }
}

static void
open_notification(indigo_app *app)
{
    const indigo_notification *n = indigo_notifications_selected(&app->notifications);

    if (!n) {
        return;
    }
    if (n->kind == INDIGO_NOTE_FOLLOW) {
        indigo_app_open_profile(app, n->handle);
    } else {
        indigo_app_open_thread(app, n->target_uri);
    }
}

void
indigo_app_update_notifications(indigo_app *app, const indigo_input *input)
{
    indigo_notifications *n = &app->notifications;
    int drag = indigo_app_drag_rows(app, input);

    if (input->up) {
        indigo_notifications_move(n, -1, INDIGO_TIMELINE_ROWS);
    }
    if (input->down) {
        indigo_notifications_move(n, 1, INDIGO_TIMELINE_ROWS);
    }
    if (input->page_up) {
        indigo_notifications_move(n, -INDIGO_TIMELINE_ROWS, INDIGO_TIMELINE_ROWS);
    }
    if (input->page_down) {
        indigo_notifications_move(n, INDIGO_TIMELINE_ROWS, INDIGO_TIMELINE_ROWS);
    }
    if (drag != 0) {
        indigo_notifications_move(n, drag, INDIGO_TIMELINE_ROWS);
    }
    if (input->back) {
        indigo_app_go_back(app);
        return;
    }
    if (input->confirm) {
        open_notification(app);
    }
    if (input->refresh && !n->loading && app->request == INDIGO_REQUEST_NONE) {
        n->loading = true;
        app->request = INDIGO_REQUEST_NOTIFICATIONS;
    }
    if (input->touch_pressed) {
        indigo_action a = indigo_layout_hit_app(app, input->touch_x, input->touch_y);

        switch (a) {
        case INDIGO_ACTION_ROW0:
        case INDIGO_ACTION_ROW1:
        case INDIGO_ACTION_ROW2:
            indigo_notifications_select(n, n->scroll + (unsigned) (a - INDIGO_ACTION_ROW0),
                                        INDIGO_TIMELINE_ROWS);
            break;
        case INDIGO_ACTION_OPEN:
            open_notification(app);
            break;
        case INDIGO_ACTION_REFRESH:
            if (!n->loading && app->request == INDIGO_REQUEST_NONE) {
                n->loading = true;
                app->request = INDIGO_REQUEST_NOTIFICATIONS;
            }
            break;
        case INDIGO_ACTION_BACK:
            indigo_app_go_back(app);
            break;
        default:
            break;
        }
    }
}
