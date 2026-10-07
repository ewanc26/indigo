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
    /* Large (the timeline is inline): zero in place, never via a temporary. */
    memset(app, 0, sizeof *app);
    app->screen = INDIGO_SCREEN_SIGNIN;
    indigo_signin_init(&app->signin);
    indigo_timeline_init(&app->timeline);
    indigo_timeline_init(&app->thread);
    /* The memset above leaves text_scale at 0, which is not a valid scale. */
    indigo_settings_defaults(&app->settings);
}

void
indigo_app_set_settings(indigo_app *app, const indigo_settings *settings)
{
    app->settings = *settings;
    indigo_settings_clamp(&app->settings);
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
        set_status(s, "Fill in the OAuth node URL and handle first.", true);
        return;
    }
    app->request = INDIGO_REQUEST_SIGN_IN;
}

bool
indigo_app_set_field(indigo_app *app, indigo_field f, const char *text)
{
    indigo_input_status st = indigo_signin_set_field(&app->signin, f, text);

    if (st != INDIGO_INPUT_OK) {
        set_status(&app->signin, indigo_input_status_message(st), true);
        return false;
    }
    set_status(&app->signin, "", false);
    return true;
}

void
indigo_app_submit(indigo_app *app)
{
    try_sign_in(app);
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
        switch (indigo_layout_hit_app(app, input->touch_x, input->touch_y)) {
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
push_screen(indigo_app *app)
{
    if (app->history_count == sizeof app->history / sizeof app->history[0]) {
        memmove(&app->history[0], &app->history[1], sizeof app->history - sizeof app->history[0]);
        app->history_count--;
    }
    app->history[app->history_count++] = app->screen;
}

static void
go_back(indigo_app *app)
{
    indigo_screen prev =
        app->history_count ? app->history[--app->history_count] : INDIGO_SCREEN_HOME;

    /* History holds screens, not search kinds, and the search screen serves
     * seven of them. Returning from a list's members to the lists is the one
     * case where the same screen is stacked on itself with a different kind:
     * the members are actors in the same union the lists live in, so both
     * the kind and the rows have to be restored by hand. */
    if (prev == INDIGO_SCREEN_SEARCH && app->screen == INDIGO_SCREEN_SEARCH
        && app->search.kind == INDIGO_SEARCH_LIST_MEMBERS && app->search.held_count > 0) {
        app->search.kind = INDIGO_SEARCH_LISTS;
        memcpy(app->search.results.lists, app->search.held_lists,
               sizeof app->search.results.lists);
        app->search.count = app->search.held_count;
        app->search.selected = 0;
        app->search.scroll = 0;
        app->search.loading = false;
    }
    app->screen = prev;
}

indigo_timeline *
indigo_app_active_list(indigo_app *app)
{
    return app->screen == INDIGO_SCREEN_THREAD ? &app->thread : &app->timeline;
}

void
indigo_app_set_like(indigo_app *app, const char *post_uri, const char *like_uri, bool pending)
{
    indigo_timeline_set_like(&app->timeline, post_uri, like_uri, pending);
    indigo_timeline_set_like(&app->thread, post_uri, like_uri, pending);
}

void
indigo_app_set_repost(indigo_app *app, const char *post_uri, const char *repost_uri,
                      bool pending)
{
    indigo_timeline_set_repost(&app->timeline, post_uri, repost_uri, pending);
    indigo_timeline_set_repost(&app->thread, post_uri, repost_uri, pending);
}

static void
request_post_action(indigo_app *app, indigo_request_kind kind, const indigo_post *post,
                    const char *undo_uri)
{
    indigo_copy_utf8(app->request_post_uri, sizeof app->request_post_uri, post->uri);
    indigo_copy_utf8(app->request_post_cid, sizeof app->request_post_cid, post->cid);
    indigo_copy_utf8(app->request_undo_uri, sizeof app->request_undo_uri, undo_uri);
    app->request = kind;
}

static void
toggle_like(indigo_app *app)
{
    const indigo_post *p = indigo_timeline_selected(indigo_app_active_list(app));

    if (!p || p->like_pending || app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    request_post_action(app, p->like_uri[0] ? INDIGO_REQUEST_UNLIKE : INDIGO_REQUEST_LIKE, p,
                        p->like_uri);
    indigo_app_set_like(app, p->uri, p->like_uri, true);
}

static void
toggle_repost(indigo_app *app)
{
    const indigo_post *p = indigo_timeline_selected(indigo_app_active_list(app));

    if (!p || p->repost_pending || app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    request_post_action(app, p->repost_uri[0] ? INDIGO_REQUEST_UNREPOST : INDIGO_REQUEST_REPOST,
                        p, p->repost_uri);
    indigo_app_set_repost(app, p->uri, p->repost_uri, true);
}

static void
refresh_timeline(indigo_app *app)
{
    if (app->timeline.loading || app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    indigo_timeline_begin_fetch(&app->timeline, true);
    if (app->feed_uri[0]) {
        /* A feed view refreshes its feed; the request fields still hold it. */
        app->request = INDIGO_REQUEST_FEED;
    } else {
        app->request = INDIGO_REQUEST_TIMELINE_REFRESH;
    }
}

static void
load_thread(indigo_app *app, const char *uri)
{
    indigo_copy_utf8(app->thread_uri, sizeof app->thread_uri, uri);
    indigo_copy_utf8(app->request_post_uri, sizeof app->request_post_uri, uri);
    indigo_timeline_begin_fetch(&app->thread, true);
    app->request = INDIGO_REQUEST_THREAD;
}

static void
open_thread(indigo_app *app, const char *uri)
{
    if (!uri[0] || app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    push_screen(app);
    app->screen = INDIGO_SCREEN_THREAD;
    load_thread(app, uri);
}

static void
open_profile(indigo_app *app, const char *actor)
{
    if (!actor[0] || app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    push_screen(app);
    app->screen = INDIGO_SCREEN_PROFILE;
    memset(&app->profile, 0, sizeof app->profile);
    app->profile.loading = true;
    indigo_copy_utf8(app->profile.handle, sizeof app->profile.handle, actor);
    indigo_copy_utf8(app->request_post_uri, sizeof app->request_post_uri, actor);
    app->request = INDIGO_REQUEST_PROFILE;
}

static void
open_notifications(indigo_app *app)
{
    if (app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    push_screen(app);
    app->screen = INDIGO_SCREEN_NOTIFICATIONS;
    app->notifications.loading = true;
    app->notifications.status[0] = '\0';
    app->request = INDIGO_REQUEST_NOTIFICATIONS;
}

static void
open_menu(indigo_app *app)
{
    push_screen(app);
    app->screen = INDIGO_SCREEN_MENU;
    indigo_menu_build(&app->menu, indigo_timeline_selected(&app->timeline),
                      app->signin.account);
}

/* The post whose image the viewer would open, from whichever screen is asking.
 * Three screens draw a post's picture; the other eight have nothing to open, so
 * they never ask. */
const indigo_post *
indigo_app_image_source(const indigo_app *app)
{
    if (app->screen == INDIGO_SCREEN_SEARCH) {
        return indigo_search_is_posts(&app->search)
                   ? indigo_search_selected_post(&app->search)
                   : NULL;
    }
    if (app->screen == INDIGO_SCREEN_THREAD) {
        return indigo_timeline_selected(&app->thread);
    }
    if (app->screen == INDIGO_SCREEN_HOME) {
        return indigo_timeline_selected(&app->timeline);
    }
    return NULL;
}

bool
indigo_app_open_image(indigo_app *app)
{
    const indigo_post *p = indigo_app_image_source(app);

    if (!indigo_post_has_image(p)) {
        return false;
    }
    /* Copied, not pointed at: the list this post is in keeps paging and can
     * reuse its slot, and a viewer that changed pictures under the reader
     * would be worse than no viewer. */
    indigo_copy_utf8(app->image.url, sizeof app->image.url, p->embed_thumb);
    indigo_copy_utf8(app->image.alt, sizeof app->image.alt, p->embed_alt);
    app->image.aspect_w = p->embed_w;
    app->image.aspect_h = p->embed_h;
    app->image.count = p->embed_count;
    push_screen(app);
    app->screen = INDIGO_SCREEN_IMAGE;
    return true;
}

void
indigo_app_close_image(indigo_app *app)
{
    if (app->screen == INDIGO_SCREEN_IMAGE) {
        go_back(app);
    }
}

/* Search keeps its query and results across visits: retyping a name to reach
 * the same list again would be the wrong trade on a system keyboard. */
static void
/* The kind is set by the caller because the menu offers both a person and a
 * post search. Either way it is set explicitly: inheriting the previous
 * list's kind would show that list's results under the wrong heading. */
open_search(indigo_app *app, indigo_search_kind kind)
{
    push_screen(app);
    app->screen = INDIGO_SCREEN_SEARCH;
    app->search.kind = kind;
    app->search.loading = false;
}

static void
submit_search(indigo_app *app)
{
    indigo_search *s = &app->search;

    if (!indigo_search_can_submit(s) || app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    indigo_search_begin_page(s, false);
    /* One screen searches two things; the kind decides which request the
     * session receives. */
    app->request = indigo_search_is_posts(s) ? INDIGO_REQUEST_POST_SEARCH : INDIGO_REQUEST_SEARCH;
}

static void
edit_query(indigo_app *app)
{
    if (app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    app->request = INDIGO_REQUEST_EDIT_QUERY;
}

/* The draft text is kept across cancel and failure: nothing typed is lost. */
static void
begin_compose(indigo_app *app, indigo_compose_mode mode, const indigo_post *target)
{
    indigo_compose *c = &app->compose;

    if (c->sending) {
        return;
    }
    push_screen(app);
    app->screen = INDIGO_SCREEN_COMPOSE;
    c->mode = mode;
    c->has_target = target != NULL;
    c->status[0] = '\0';
    c->status_is_error = false;
    if (target) {
        c->target = *target;
        indigo_copy_utf8(c->root_uri, sizeof c->root_uri,
                         app->thread.count ? app->thread.posts[0].uri : target->uri);
        indigo_copy_utf8(c->root_cid, sizeof c->root_cid,
                         app->thread.count ? app->thread.posts[0].cid : target->cid);
    }
    if (c->text[0]) {
        indigo_copy_utf8(c->status, sizeof c->status, "Draft kept from earlier.");
    }
}

static void
send_compose(indigo_app *app)
{
    indigo_compose *c = &app->compose;

    if (!indigo_compose_ready(c) || app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    c->sending = true;
    c->status[0] = '\0';
    app->request = INDIGO_REQUEST_PUBLISH;
}

/* Navigation and like/repost shared by the timeline and thread lists. */
/* The rows a touch drag has moved a list by this frame: positive when the finger
 * went up, so later rows come into view. Only a drag that began on one of the
 * list's rows counts, so dragging from a button or the header never scrolls. */
static int
drag_rows(const indigo_app *app, const indigo_input *input)
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
    int drag = drag_rows(app, input);

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
        toggle_like(app);
    }
    if (input->repost) {
        toggle_repost(app);
    }
}

/* A feed view's B returns to the picker it came from, and the home screen goes
 * back to the Following timeline. */
static void
leave_feed(indigo_app *app)
{
    app->feed_uri[0] = '\0';
    app->feed_name[0] = '\0';
    go_back(app);
    indigo_timeline_begin_fetch(&app->timeline, true);
    app->request = INDIGO_REQUEST_TIMELINE_REFRESH;
}

static void
update_home(indigo_app *app, const indigo_input *input)
{
    indigo_timeline *t = &app->timeline;
    const indigo_post *sel = indigo_timeline_selected(t);

    update_list(app, input, t);
    if (input->confirm && sel) {
        open_thread(app, sel->uri);
    }
    if (input->back) {
        if (app->feed_uri[0]) {
            leave_feed(app);
        } else {
            open_menu(app);
        }
    }
    if (input->refresh) {
        refresh_timeline(app);
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
            indigo_timeline_select(t, t->scroll + (unsigned) (a - INDIGO_ACTION_ROW0),
                                   INDIGO_TIMELINE_ROWS);
            break;
        case INDIGO_ACTION_LIKE:
            toggle_like(app);
            break;
        case INDIGO_ACTION_REPOST:
            toggle_repost(app);
            break;
        case INDIGO_ACTION_IMAGE:
            indigo_app_open_image(app);
            break;
        case INDIGO_ACTION_OPEN:
            if (sel) {
                open_thread(app, sel->uri);
            }
            break;
        case INDIGO_ACTION_REFRESH:
            refresh_timeline(app);
            break;
        case INDIGO_ACTION_MENU:
            /* The bottom-right button does what B does on this screen. */
            if (app->feed_uri[0]) {
                leave_feed(app);
            } else {
                open_menu(app);
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

static void
update_thread(indigo_app *app, const indigo_input *input)
{
    indigo_timeline *t = &app->thread;
    const indigo_post *sel = indigo_timeline_selected(t);

    update_list(app, input, t);
    if (input->back) {
        go_back(app);
        return;
    }
    if (input->confirm && sel) {
        begin_compose(app, INDIGO_COMPOSE_REPLY, sel);
    }
    if (input->refresh && sel) {
        open_profile(app, sel->handle);
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
            indigo_timeline_select(t, t->scroll + (unsigned) (a - INDIGO_ACTION_ROW0),
                                   INDIGO_TIMELINE_ROWS);
            break;
        case INDIGO_ACTION_LIKE:
            toggle_like(app);
            break;
        case INDIGO_ACTION_REPOST:
            toggle_repost(app);
            break;
        case INDIGO_ACTION_IMAGE:
            indigo_app_open_image(app);
            break;
        case INDIGO_ACTION_REPLY:
            if (sel) {
                begin_compose(app, INDIGO_COMPOSE_REPLY, sel);
            }
            break;
        case INDIGO_ACTION_AUTHOR:
            if (sel) {
                open_profile(app, sel->handle);
            }
            break;
        case INDIGO_ACTION_BACK:
            go_back(app);
            break;
        default:
            break;
        }
    }
}

static void
update_profile(indigo_app *app, const indigo_input *input)
{
    indigo_profile *p = &app->profile;

    if (input->back) {
        go_back(app);
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
                open_thread(app, p->pinned_uri);
            }
            break;
        case INDIGO_ACTION_BACK:
            go_back(app);
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
        open_profile(app, n->handle);
    } else {
        open_thread(app, n->target_uri);
    }
}

static void
update_notifications(indigo_app *app, const indigo_input *input)
{
    indigo_notifications *n = &app->notifications;
    int drag = drag_rows(app, input);

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
        go_back(app);
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
            go_back(app);
            break;
        default:
            break;
        }
    }
}

#define MENU_ITEMS INDIGO_MENU_ROWS

/* Shown on the screen we return to: a link or tag cannot be opened here, so
 * it is at least readable. */
static void
say(indigo_app *app, const char *text)
{
    indigo_timeline *list = indigo_app_active_list(app);

    indigo_copy_utf8(list->status, sizeof list->status, text);
    list->status_is_error = false;
}

static void
menu_choose(indigo_app *app, unsigned item)
{
    const indigo_menu_item *it =
        item < app->menu.count ? &app->menu.items[item] : NULL;

    if (!it) {
        return;
    }
    switch (it->kind) {
    case INDIGO_MENU_OPEN_MENTION:
        go_back(app);
        open_profile(app, it->payload);
        break;
    case INDIGO_MENU_SHOW_TAG:
    case INDIGO_MENU_SHOW_LINK:
        go_back(app);
        say(app, it->label);
        break;
    case INDIGO_MENU_LIKED_BY:
    case INDIGO_MENU_REPOSTED_BY: {
        char uri[INDIGO_POST_URI_MAX];

        /* Copied first: go_back leaves the menu screen and the list is about
         * to be rebuilt from the post, not the menu. The list is pushed so B
         * returns to the timeline the post is on. */
        indigo_copy_utf8(uri, sizeof uri, app->menu.post_uri);
        go_back(app);
        push_screen(app);
        indigo_app_open_people(app,
                               it->kind == INDIGO_MENU_LIKED_BY ? INDIGO_SEARCH_LIKED_BY
                                                                : INDIGO_SEARCH_REPOSTED_BY,
                               uri);
        break;
    }
    case INDIGO_MENU_COMPOSE:
        go_back(app);
        begin_compose(app, INDIGO_COMPOSE_POST, NULL);
        break;
    case INDIGO_MENU_NOTIFICATIONS:
        go_back(app);
        open_notifications(app);
        break;
    case INDIGO_MENU_FIND_PEOPLE:
        go_back(app);
        open_search(app, INDIGO_SEARCH_PEOPLE);
        break;
    case INDIGO_MENU_FIND_POSTS:
        go_back(app);
        open_search(app, INDIGO_SEARCH_POSTS);
        break;
    case INDIGO_MENU_LISTS:
        go_back(app);
        indigo_app_open_lists(app);
        break;
    case INDIGO_MENU_FEEDS:
        go_back(app);
        indigo_app_open_feeds(app);
        break;
    case INDIGO_MENU_MUTED:
        go_back(app);
        indigo_app_open_mutes(app);
        break;
    case INDIGO_MENU_BLOCKED:
        go_back(app);
        indigo_app_open_blocks(app);
        break;
    case INDIGO_MENU_MY_PROFILE:
        go_back(app);
        open_profile(app, app->signin.account);
        break;
    case INDIGO_MENU_SETTINGS:
        go_back(app);
        indigo_app_open_settings(app);
        break;
    case INDIGO_MENU_UPDATE:
        go_back(app);
        indigo_app_open_update(app);
        break;
    case INDIGO_MENU_SIGN_OUT:
        app->request = INDIGO_REQUEST_SIGN_OUT;
        break;
    case INDIGO_MENU_CLOSE:
        go_back(app);
        break;
    }
}

static void
update_menu(indigo_app *app, const indigo_input *input)
{
    int drag = drag_rows(app, input);

    if (input->up) {
        indigo_menu_move(&app->menu, -1, MENU_ITEMS);
    }
    if (input->down) {
        indigo_menu_move(&app->menu, 1, MENU_ITEMS);
    }
    if (drag != 0) {
        indigo_menu_move(&app->menu, drag, MENU_ITEMS);
    }
    if (input->back) {
        go_back(app);
        return;
    }
    if (input->confirm) {
        menu_choose(app, app->menu.selected);
        return;
    }
    if (input->touch_pressed) {
        indigo_action a = indigo_layout_hit_app(app, input->touch_x, input->touch_y);

        if (a == INDIGO_ACTION_BACK) {
            go_back(app);
        } else if (a >= INDIGO_ACTION_MENU0 && a <= INDIGO_ACTION_MENU4) {
            unsigned row = (unsigned) (a - INDIGO_ACTION_MENU0);

            menu_choose(app, app->menu.scroll + row);
        }
    }
}

/* Open the search screen's selected row: a thread for a post, a member list
 * for a curated list, a profile for a person. The list-members kind opens a
 * profile through the member's handle; a list row itself carries only a URI,
 * which open_profile cannot take. */
static void
open_search_selection(indigo_app *app)
{
    indigo_search *s = &app->search;

    if (indigo_search_is_posts(s)) {
        const indigo_post *p = indigo_search_selected_post(s);

        if (p) {
            open_thread(app, p->uri);
        }
    } else if (s->kind == INDIGO_SEARCH_LISTS) {
        const indigo_list *l = indigo_search_selected_list(s);

        if (l) {
            indigo_app_open_list_members(app, l->uri, l->name);
        }
    } else if (s->kind == INDIGO_SEARCH_FEEDS) {
        const indigo_list *f = indigo_search_selected_list(s);

        if (f) {
            indigo_app_open_feed(app, f->uri, f->name);
        }
    } else if (s->kind == INDIGO_SEARCH_LIST_MEMBERS) {
        const indigo_actor *a = indigo_search_selected(s);

        if (a) {
            open_profile(app, a->handle);
        }
    } else {
        const indigo_actor *a = indigo_search_selected(s);

        if (a) {
            open_profile(app, a->handle);
        }
    }
}

static void
update_search(indigo_app *app, const indigo_input *input)
{
    indigo_search *s = &app->search;
    int drag = drag_rows(app, input);

    if (input->up) {
        indigo_search_move(s, -1, INDIGO_SEARCH_ROWS);
    }
    if (input->down) {
        indigo_search_move(s, 1, INDIGO_SEARCH_ROWS);
    }
    if (input->page_up) {
        indigo_search_move(s, -INDIGO_SEARCH_ROWS, INDIGO_SEARCH_ROWS);
    }
    if (input->page_down) {
        indigo_search_move(s, INDIGO_SEARCH_ROWS, INDIGO_SEARCH_ROWS);
    }
    if (drag != 0) {
        indigo_search_move(s, drag, INDIGO_SEARCH_ROWS);
    }
    if (input->back) {
        go_back(app);
        return;
    }
    /* Typing the query and running it are one action: the keyboard blocks, so
     * making the person confirm again on a list screen would be a wasted
     * round trip through a system dialog. The followers and following lists
     * have nothing to type, so the same press only opens the keyboard in the
     * search mode. */
    if (input->confirm && indigo_search_is_typed(s)) {
        edit_query(app);
    }
    /* SEL already opens the selected person's profile on the thread screen. */
    if (input->refresh) {
        open_search_selection(app);
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
            indigo_search_select(s, s->scroll + (unsigned) (a - INDIGO_ACTION_ROW0),
                                 INDIGO_SEARCH_ROWS);
            break;
        case INDIGO_ACTION_FIELD_QUERY:
            if (indigo_search_is_typed(s)) {
                edit_query(app);
            }
            break;
        case INDIGO_ACTION_AUTHOR:
            open_search_selection(app);
            break;
        case INDIGO_ACTION_IMAGE:
            /* Only a post result has an image, and opening one from an actor
             * row is a no-op, so a tap that lands on a button which is not
             * drawn does nothing rather than opening the wrong thing. */
            indigo_app_open_image(app);
            break;
        case INDIGO_ACTION_BACK:
            go_back(app);
            break;
        default:
            break;
        }
    }
    /* The same prefetch rule as the timeline: near the end of what is held,
     * ask for the next page before the person reaches the bottom. */
    if (app->request == INDIGO_REQUEST_NONE && indigo_search_wants_page(s)) {
        indigo_request_kind request = INDIGO_REQUEST_NONE;

        switch (s->kind) {
        case INDIGO_SEARCH_PEOPLE:
            request = INDIGO_REQUEST_SEARCH;
            break;
        case INDIGO_SEARCH_POSTS:
            request = INDIGO_REQUEST_POST_SEARCH;
            break;
        case INDIGO_SEARCH_AUTHOR:
            request = INDIGO_REQUEST_AUTHOR_FEED;
            break;
        case INDIGO_SEARCH_FOLLOWERS:
        case INDIGO_SEARCH_FOLLOWING:
        case INDIGO_SEARCH_LIKED_BY:
        case INDIGO_SEARCH_REPOSTED_BY:
            request = INDIGO_REQUEST_PEOPLE;
            app->request_people = s->kind;
            break;
        case INDIGO_SEARCH_LISTS:
            request = INDIGO_REQUEST_LISTS;
            break;
        case INDIGO_SEARCH_LIST_MEMBERS:
            request = INDIGO_REQUEST_LIST_MEMBERS;
            break;
        case INDIGO_SEARCH_MUTED:
            request = INDIGO_REQUEST_MUTES;
            break;
        case INDIGO_SEARCH_BLOCKED:
            request = INDIGO_REQUEST_BLOCKS;
            break;
        case INDIGO_SEARCH_FEEDS:
        default:
            /* Saved feeds come from preferences in one shot; there is no
             * second page to ask for. */
            break;
        }
        if (request != INDIGO_REQUEST_NONE) {
            indigo_search_begin_page(s, true);
            app->request = request;
        }
    }
}

/* The Y button and the pill under the draft do the same job, and which job
 * depends on the mode. A top-level post has no reply/quote to switch between,
 * so there its second button picks the reply gate instead. */
static void
compose_second_button(indigo_app *app)
{
    indigo_compose *c = &app->compose;

    if (indigo_compose_can_gate(c)) {
        indigo_compose_gate_cycle(c);
    } else {
        indigo_compose_toggle(c);
    }
}

static void
update_compose(indigo_app *app, const indigo_input *input)
{
    indigo_compose *c = &app->compose;

    if (c->sending) {
        return;
    }
    if (input->back) {
        go_back(app);
        return;
    }
    if (input->confirm && app->request == INDIGO_REQUEST_NONE) {
        app->request = INDIGO_REQUEST_EDIT_DRAFT;
    }
    if (input->like) {
        compose_second_button(app);
    }
    if (input->repost) {
        send_compose(app);
    }
    if (input->touch_pressed) {
        switch (indigo_layout_hit_app(app, input->touch_x, input->touch_y)) {
        case INDIGO_ACTION_EDIT:
            if (app->request == INDIGO_REQUEST_NONE) {
                app->request = INDIGO_REQUEST_EDIT_DRAFT;
            }
            break;
        case INDIGO_ACTION_TOGGLE:
            compose_second_button(app);
            break;
        case INDIGO_ACTION_SEND:
            send_compose(app);
            break;
        case INDIGO_ACTION_BACK:
            go_back(app);
            break;
        default:
            break;
        }
    }
}

static void
toggle_setting_row(indigo_app *app, unsigned row)
{
    indigo_settings *s = &app->settings;

    switch (row) {
    case 0:
        s->theme = (indigo_theme) (((int) s->theme + 1) % 3);
        break;
    case 1:
        if (s->text_scale == INDIGO_TEXT_SCALE_SMALL) {
            s->text_scale = INDIGO_TEXT_SCALE_NORMAL;
        } else if (s->text_scale == INDIGO_TEXT_SCALE_NORMAL) {
            s->text_scale = INDIGO_TEXT_SCALE_LARGE;
        } else {
            s->text_scale = INDIGO_TEXT_SCALE_SMALL;
        }
        break;
    case 2:
        s->reduce_motion = !s->reduce_motion;
        break;
    case 3:
        s->high_contrast = !s->high_contrast;
        break;
    case 4:
        s->large_targets = !s->large_targets;
        break;
    case 5:
        s->alt_text = !s->alt_text;
        break;
    case 6:
        s->diagnostics = !s->diagnostics;
        break;
    case 7:
        if (s->default_feed[0]) {
            s->default_feed[0] = '\0';
        } else if (app->feed_uri[0]) {
            indigo_copy_utf8(s->default_feed, sizeof s->default_feed, app->feed_uri);
        }
        break;
    default:
        return;
    }

    indigo_settings_clamp(s);
    app->request = INDIGO_REQUEST_SAVE_SETTINGS;
}

static void
update_settings(indigo_app *app, const indigo_input *input)
{
    if (input->up) {
        if (app->settings_selected > 0) {
            app->settings_selected--;
        }
    }
    if (input->down) {
        if (app->settings_selected < 7) {
            app->settings_selected++;
        }
    }
    if (input->back) {
        go_back(app);
        return;
    }
    if (input->confirm) {
        toggle_setting_row(app, app->settings_selected);
        return;
    }
    if (input->touch_pressed) {
        indigo_action a = indigo_layout_hit_app(app, input->touch_x, input->touch_y);
        if (a == INDIGO_ACTION_BACK) {
            go_back(app);
            return;
        }
        if (a >= INDIGO_ACTION_SETTINGS_ROW0 && a <= INDIGO_ACTION_SETTINGS_ROW7) {
            unsigned row = (unsigned) (a - INDIGO_ACTION_SETTINGS_ROW0);
            app->settings_selected = row;
            toggle_setting_row(app, row);
        }
    }
}

/* The update screen has one button, and what it does depends on where the
 * update is: check, or -- only once the person has been shown the version --
 * install. The state machine says which; this only asks it. */
static void
press_update(indigo_app *app)
{
    switch (indigo_updater_action_for(&app->updater)) {
    case INDIGO_UPDATER_ACT_CHECK:
        indigo_updater_begin_check(&app->updater);
        app->request = INDIGO_REQUEST_UPDATE_CHECK;
        break;
    case INDIGO_UPDATER_ACT_INSTALL:
        indigo_updater_begin_install(&app->updater);
        app->request = INDIGO_REQUEST_UPDATE_INSTALL;
        break;
    case INDIGO_UPDATER_ACT_NONE:
        break;
    }
}

static void
update_update(indigo_app *app, const indigo_input *input)
{
    if (input->back) {
        go_back(app);
        return;
    }
    if (input->confirm) {
        press_update(app);
        return;
    }
    if (input->touch_pressed) {
        indigo_action a = indigo_layout_hit_app(app, input->touch_x, input->touch_y);

        if (a == INDIGO_ACTION_BACK) {
            go_back(app);
        } else if (a == INDIGO_ACTION_UPDATE) {
            press_update(app);
        }
    }
}

/* The viewer has no state of its own to change: B and the Close button both
 * leave, and nothing on either screen can be pressed. ZR is the shortcut for
 * the same thing, because it is the shortcut that opened it. */
static void
update_image(indigo_app *app, const indigo_input *input)
{
    if (input->back || input->zr) {
        indigo_app_close_image(app);
        return;
    }
    if (input->touch_pressed &&
        indigo_layout_hit_app(app, input->touch_x, input->touch_y) == INDIGO_ACTION_BACK) {
        indigo_app_close_image(app);
    }
}

void
indigo_app_update(indigo_app *app, const indigo_input *input)
{
    if (input->quit) {
        app->quit_requested = true;
    }

    switch (app->screen) {
    case INDIGO_SCREEN_SIGNIN:
        update_signin(app, input);
        break;
    case INDIGO_SCREEN_HOME:
        update_home(app, input);
        break;
    case INDIGO_SCREEN_THREAD:
        update_thread(app, input);
        break;
    case INDIGO_SCREEN_PROFILE:
        update_profile(app, input);
        break;
    case INDIGO_SCREEN_NOTIFICATIONS:
        update_notifications(app, input);
        break;
    case INDIGO_SCREEN_MENU:
        update_menu(app, input);
        break;
    case INDIGO_SCREEN_COMPOSE:
        update_compose(app, input);
        break;
    case INDIGO_SCREEN_SEARCH:
        update_search(app, input);
        break;
    case INDIGO_SCREEN_IMAGE:
        update_image(app, input);
        break;
    case INDIGO_SCREEN_SETTINGS:
        update_settings(app, input);
        break;
    case INDIGO_SCREEN_UPDATE:
        update_update(app, input);
        break;
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
indigo_app_peek_request(const indigo_app *app)
{
    return app->request;
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
    app->history_count = 0;
    indigo_timeline_begin_fetch(&app->timeline, true);

    /* A default feed replaces the Following timeline for the first screen
     * only. indigo_app_open_feed() is the wrong entry point here: it pushes a
     * history entry, which at startup would send B from Home back to sign-in.
     * The name is the record key's until the feed is named by a later lookup;
     * the picker is where a named title comes from. */
    if (app->settings.default_feed[0]) {
        indigo_copy_utf8(app->feed_uri, sizeof app->feed_uri,
                         app->settings.default_feed);
        indigo_copy_utf8(app->feed_name, sizeof app->feed_name, "Feed");
        indigo_copy_utf8(app->request_feed_uri, sizeof app->request_feed_uri,
                         app->settings.default_feed);
        indigo_copy_utf8(app->request_feed_name, sizeof app->request_feed_name,
                         "Feed");
        app->request = INDIGO_REQUEST_FEED;
        return;
    }
    app->request = INDIGO_REQUEST_TIMELINE_REFRESH;
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
    indigo_timeline_clear(&app->timeline);
    indigo_timeline_clear(&app->thread);
    indigo_notifications_clear(&app->notifications);
    memset(&app->profile, 0, sizeof app->profile);
    app->history_count = 0;
    set_status(s, message, false);
    app->screen = INDIGO_SCREEN_SIGNIN;
}

void
indigo_app_thread_loaded(indigo_app *app, const indigo_post *posts, unsigned count,
                         unsigned focus)
{
    indigo_timeline *t = &app->thread;

    indigo_timeline_clear(t);
    for (unsigned i = 0; posts && i < count; i++) {
        if (!indigo_timeline_append(t, &posts[i])) {
            break;
        }
    }
    app->thread_focus = focus < t->count ? focus : 0;
    indigo_timeline_select(t, app->thread_focus, INDIGO_TIMELINE_ROWS);
    if (t->count == 0) {
        indigo_timeline_fail_fetch(t, "That post is not available.");
    }
}

void
indigo_app_thread_failed(indigo_app *app, const char *message)
{
    indigo_timeline_fail_fetch(&app->thread, message);
}

void
indigo_app_profile_loaded(indigo_app *app, const indigo_profile *p)
{
    app->profile = *p;
    app->profile.loading = false;
}

void
indigo_app_profile_failed(indigo_app *app, const char *message)
{
    app->profile.loading = false;
    indigo_copy_utf8(app->profile.status, sizeof app->profile.status, message);
    app->profile.status_is_error = true;
}

void
indigo_app_notifications_loaded(indigo_app *app, const indigo_notification *items,
                                unsigned count)
{
    indigo_notifications *n = &app->notifications;

    indigo_notifications_clear(n);
    for (unsigned i = 0; items && i < count && i < INDIGO_NOTIFICATION_MAX; i++) {
        n->items[n->count++] = items[i];
    }
    if (n->count == 0) {
        indigo_copy_utf8(n->status, sizeof n->status, "No notifications yet.");
    }
}

void
indigo_app_notifications_failed(indigo_app *app, const char *message)
{
    app->notifications.loading = false;
    indigo_copy_utf8(app->notifications.status, sizeof app->notifications.status, message);
    app->notifications.status_is_error = true;
}

void
indigo_app_set_query(indigo_app *app, const char *text)
{
    indigo_search *s = &app->search;

    indigo_copy_utf8(s->query, sizeof s->query, text ? text : "");
    /* A changed query makes the old results stale, and leaving them up would
     * invite opening a profile for someone the new query never matched. */
    s->count = 0;
    s->selected = 0;
    s->scroll = 0;
    s->searched = false;
    s->status[0] = '\0';
    s->status_is_error = false;
    submit_search(app);
}

void
indigo_app_search_loaded(indigo_app *app, const indigo_actor *actors, unsigned count,
                         const char *next_cursor)
{
    indigo_search *s = &app->search;

    for (unsigned i = 0; actors && i < count && s->count < INDIGO_SEARCH_MAX; i++) {
        s->results.actors[s->count++] = actors[i];
    }
    indigo_search_finish_page(s, count, next_cursor);
    if (s->count == 0) {
        indigo_copy_utf8(s->status, sizeof s->status, "Nobody matched that.");
    }
}

void
indigo_app_search_failed(indigo_app *app, const char *message)
{
    indigo_search_fail_page(&app->search, message);
}

void
indigo_app_open_people(indigo_app *app, indigo_search_kind kind, const char *subject)
{
    if (kind == INDIGO_SEARCH_PEOPLE || !subject || !subject[0]) {
        return;
    }
    app->screen = INDIGO_SCREEN_SEARCH;
    app->search.kind = kind;
    indigo_search_begin_page(&app->search, false);
    indigo_copy_utf8(app->search.subject, sizeof app->search.subject, subject);
    app->request_people = kind;
    indigo_copy_utf8(app->request_subject, sizeof app->request_subject, subject);
    app->request = INDIGO_REQUEST_PEOPLE;
}

void
indigo_app_open_author_posts(indigo_app *app, const char *actor)
{
    if (!actor || !actor[0]) {
        return;
    }
    app->screen = INDIGO_SCREEN_SEARCH;
    app->search.kind = INDIGO_SEARCH_AUTHOR;
    indigo_search_begin_page(&app->search, false);
    indigo_copy_utf8(app->search.subject, sizeof app->search.subject, actor);
    indigo_copy_utf8(app->request_actor, sizeof app->request_actor, actor);
    app->request = INDIGO_REQUEST_AUTHOR_FEED;
}

void
indigo_app_open_lists(indigo_app *app)
{
    push_screen(app);
    app->screen = INDIGO_SCREEN_SEARCH;
    app->search.kind = INDIGO_SEARCH_LISTS;
    /* A fresh fetch replaces the held lists too: they were the members'
     * Back target, and a new fetch makes them stale. */
    indigo_search_begin_page(&app->search, false);
    memset(app->search.held_lists, 0, sizeof app->search.held_lists);
    app->search.held_count = 0;
    app->request = INDIGO_REQUEST_LISTS;
}

void
indigo_app_open_feeds(indigo_app *app)
{
    push_screen(app);
    app->screen = INDIGO_SCREEN_SEARCH;
    app->search.kind = INDIGO_SEARCH_FEEDS;
    indigo_search_begin_page(&app->search, false);
    app->request = INDIGO_REQUEST_FEEDS;
}

void
indigo_app_open_mutes(indigo_app *app)
{
    push_screen(app);
    app->screen = INDIGO_SCREEN_SEARCH;
    app->search.kind = INDIGO_SEARCH_MUTED;
    indigo_search_begin_page(&app->search, false);
    app->request = INDIGO_REQUEST_MUTES;
}

void
indigo_app_open_blocks(indigo_app *app)
{
    push_screen(app);
    app->screen = INDIGO_SCREEN_SEARCH;
    app->search.kind = INDIGO_SEARCH_BLOCKED;
    indigo_search_begin_page(&app->search, false);
    app->request = INDIGO_REQUEST_BLOCKS;
}

void
indigo_app_open_feed(indigo_app *app, const char *feed_uri, const char *name)
{
    if (!feed_uri || !feed_uri[0]) {
        return;
    }
    /* The picker stays on the search screen; the feed shows on the home
     * screen, so Back lands on the picker again. */
    push_screen(app);
    indigo_copy_utf8(app->feed_uri, sizeof app->feed_uri, feed_uri);
    indigo_copy_utf8(app->feed_name, sizeof app->feed_name,
                     name && name[0] ? name : "Feed");
    indigo_copy_utf8(app->request_feed_uri, sizeof app->request_feed_uri, feed_uri);
    indigo_copy_utf8(app->request_feed_name, sizeof app->request_feed_name,
                     name && name[0] ? name : "Feed");
    app->screen = INDIGO_SCREEN_HOME;
    indigo_timeline_begin_fetch(&app->timeline, true);
    app->request = INDIGO_REQUEST_FEED;
}

void
indigo_app_feeds_loaded(indigo_app *app, const indigo_list *feeds, unsigned count,
                        const char *next_cursor)
{
    indigo_search *s = &app->search;

    if (s->kind != INDIGO_SEARCH_FEEDS) {
        return;
    }
    for (unsigned i = 0; i < count && s->count < INDIGO_SEARCH_MAX; i++) {
        s->results.lists[i] = feeds[i];
        s->count++;
    }
    indigo_search_finish_page(s, count, next_cursor);
    if (s->count == 0) {
        indigo_copy_utf8(s->status, sizeof s->status, "No saved feeds.");
        s->status_is_error = false;
    }
}

void
indigo_app_open_list_members(indigo_app *app, const char *list_uri, const char *name)
{
    indigo_search *s = &app->search;

    if (!list_uri || !list_uri[0]) {
        return;
    }
    /* Keep the lists that are being browsed: their members are actors and
     * land in the same union, so without this the members would overwrite
     * the lists and Back would return to a corrupted screen. */
    if (s->kind == INDIGO_SEARCH_LISTS) {
        memcpy(s->held_lists, s->results.lists, sizeof s->held_lists);
        s->held_count = s->count;
    }
    push_screen(app);
    app->screen = INDIGO_SCREEN_SEARCH;
    s->kind = INDIGO_SEARCH_LIST_MEMBERS;
    indigo_search_begin_page(s, false);
    indigo_copy_utf8(s->subject, sizeof s->subject, name && name[0] ? name : "Members");
    indigo_copy_utf8(app->request_list_uri, sizeof app->request_list_uri, list_uri);
    app->request = INDIGO_REQUEST_LIST_MEMBERS;
}

void
indigo_app_lists_loaded(indigo_app *app, const indigo_list *lists, unsigned count,
                        const char *next_cursor)
{
    indigo_search *s = &app->search;

    if (s->kind != INDIGO_SEARCH_LISTS) {
        return;
    }
    for (unsigned i = 0; i < count && s->count < INDIGO_SEARCH_MAX; i++) {
        s->results.lists[i] = lists[i];
        s->count++;
    }
    indigo_search_finish_page(s, count, next_cursor);
    if (s->count == 0) {
        indigo_copy_utf8(s->status, sizeof s->status, "No lists yet.");
        s->status_is_error = false;
    }
}

void
indigo_app_post_search_loaded(indigo_app *app, const indigo_post *posts, unsigned count,
                               const char *next_cursor)
{
    indigo_search *s = &app->search;

    for (unsigned i = 0; i < count && s->count < INDIGO_SEARCH_MAX; i++) {
        s->results.posts[s->count++] = posts[i];
    }
    indigo_search_finish_page(s, count, next_cursor);
    if (s->count == 0) {
        indigo_copy_utf8(s->status, sizeof s->status, "No posts matched that.");
    }
}

void
indigo_app_toggle_follow(indigo_app *app)
{
    indigo_profile *p = &app->profile;

    if (p->follow_busy || app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    /* Following needs the did, which only a loaded profile carries. Guarded
     * here rather than only at the call site, because main.c drives the
     * request from app state this function has to have set up. */
    if (!p->loaded || p->loading) {
        return;
    }
    /* An unfollow needs the record URI. Without it there is nothing to
     * delete, so the button stays out rather than failing after the press. */
    if (p->following && !p->follow_uri[0]) {
        indigo_copy_utf8(p->status, sizeof p->status, "Reload the profile first.");
        p->status_is_error = true;
        return;
    }
    p->following = !p->following;
    p->follow_busy = true;
    p->status[0] = '\0';
    p->status_is_error = false;
    app->request = INDIGO_REQUEST_FOLLOW;
    /* app->profile.following is already the intended state; the failure path
     * flips it back if the server disagrees. */
    app->request_follow = p->following;
}

void
indigo_app_follow_done(indigo_app *app, bool following, const char *follow_uri)
{
    indigo_profile *p = &app->profile;

    p->follow_busy = false;
    p->following = following;
    if (following && follow_uri && follow_uri[0]) {
        indigo_copy_utf8(p->follow_uri, sizeof p->follow_uri, follow_uri);
    } else {
        /* Unfollowed: the record is gone, so keeping the URI would make a
         * second unfollow try to delete something that no longer exists. */
        p->follow_uri[0] = '\0';
    }
}

void
indigo_app_follow_failed(indigo_app *app, const char *message)
{
    indigo_profile *p = &app->profile;

    p->follow_busy = false;
    p->following = !p->following;
    indigo_copy_utf8(p->status, sizeof p->status, message);
    p->status_is_error = true;
}

/* Mute and block share a shape: flip the flag optimistically, mark the
 * request, and put it back if the write fails. Blocking needs the record URI
 * to undo itself, so like unfollowing it refuses without one. */
void
indigo_app_toggle_mute(indigo_app *app)
{
    indigo_profile *p = &app->profile;

    if (!p->loaded || p->loading || p->mute_busy || app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    p->muted = !p->muted;
    p->mute_busy = true;
    app->request_graph = p->muted ? INDIGO_GRAPH_MUTE : INDIGO_GRAPH_UNMUTE;
    app->request = INDIGO_REQUEST_GRAPH;
}

void
indigo_app_toggle_block(indigo_app *app)
{
    indigo_profile *p = &app->profile;

    if (!p->loaded || p->loading || p->block_busy || app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    if (p->blocked) {
        /* Unblocking deletes a record by URI, so without one there is nothing
         * to delete and the press is refused rather than failing after it. */
        if (!p->block_uri[0]) {
            indigo_copy_utf8(p->status, sizeof p->status, "Reload the profile first.");
            p->status_is_error = true;
            return;
        }
        app->request_graph = INDIGO_GRAPH_UNBLOCK;
        p->blocked = false;
    } else {
        p->blocked = true;
        app->request_graph = INDIGO_GRAPH_BLOCK;
    }
    p->block_busy = true;
    app->request = INDIGO_REQUEST_GRAPH;
}

void
indigo_app_graph_done(indigo_app *app, indigo_graph_action action, const char *block_uri)
{
    indigo_profile *p = &app->profile;

    switch (action) {
    case INDIGO_GRAPH_MUTE:
        p->mute_busy = false;
        p->muted = true;
        break;
    case INDIGO_GRAPH_UNMUTE:
        p->mute_busy = false;
        p->muted = false;
        break;
    case INDIGO_GRAPH_BLOCK:
        p->block_busy = false;
        p->blocked = true;
        indigo_copy_utf8(p->block_uri, sizeof p->block_uri,
                         block_uri && block_uri[0] ? block_uri : "");
        break;
    case INDIGO_GRAPH_UNBLOCK:
        p->block_busy = false;
        p->blocked = false;
        p->block_uri[0] = '\0';
        break;
    case INDIGO_GRAPH_NONE:
        break;
    }
}

void
indigo_app_graph_failed(indigo_app *app, indigo_graph_action action, const char *message)
{
    indigo_profile *p = &app->profile;

    switch (action) {
    case INDIGO_GRAPH_MUTE:
        p->mute_busy = false;
        p->muted = false;
        break;
    case INDIGO_GRAPH_UNMUTE:
        p->mute_busy = false;
        p->muted = true;
        break;
    case INDIGO_GRAPH_BLOCK:
        p->block_busy = false;
        p->blocked = false;
        p->block_uri[0] = '\0';
        break;
    case INDIGO_GRAPH_UNBLOCK:
        p->block_busy = false;
        p->blocked = true;
        break;
    case INDIGO_GRAPH_NONE:
        break;
    }
    indigo_copy_utf8(p->status, sizeof p->status, message);
    p->status_is_error = true;
}

void
indigo_app_set_draft(indigo_app *app, const char *text)
{
    indigo_copy_utf8(app->compose.text, sizeof app->compose.text, text);
    app->compose.status[0] = '\0';
}

void
indigo_app_publish_done(indigo_app *app)
{
    indigo_compose *c = &app->compose;
    indigo_compose_mode mode = c->mode;

    c->sending = false;
    c->text[0] = '\0';
    c->status[0] = '\0';
    c->has_target = false;
    if (app->screen == INDIGO_SCREEN_COMPOSE) {
        go_back(app);
    }
    /* Show the person what they just published. */
    if (mode == INDIGO_COMPOSE_REPLY && app->thread_uri[0]) {
        load_thread(app, app->thread_uri);
    } else {
        refresh_timeline(app);
    }
}

void
indigo_app_publish_failed(indigo_app *app, const char *message)
{
    indigo_compose *c = &app->compose;

    c->sending = false;
    indigo_copy_utf8(c->status, sizeof c->status, message);
    c->status_is_error = true;
}

void
indigo_app_set_updater(indigo_app *app, const char *describe, bool can_swap)
{
    indigo_updater_init(&app->updater, describe, can_swap);
}

void
indigo_app_open_update(indigo_app *app)
{
    push_screen(app);
    app->screen = INDIGO_SCREEN_UPDATE;
}

void
indigo_app_open_settings(indigo_app *app)
{
    push_screen(app);
    app->screen = INDIGO_SCREEN_SETTINGS;
    app->settings_selected = 0;
}
