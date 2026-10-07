#include "app/app_internal.h"

void
indigo_app_set_status(indigo_signin *s, const char *msg, bool is_error)
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
    indigo_copy_utf8(app->images_dir, sizeof app->images_dir, INDIGO_IMAGES_DIR);
    indigo_copy_utf8(app->camera_dir, sizeof app->camera_dir, INDIGO_CAMERA_DIR);
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
        indigo_app_set_status(s, s->handle[0] ? "Fill in the OAuth node URL first."
                                              : "Fill in the handle first.", true);
        return;
    }
    app->request = INDIGO_REQUEST_SIGN_IN;
}

bool
indigo_app_set_field(indigo_app *app, indigo_field f, const char *text)
{
    indigo_input_status st = indigo_signin_set_field(&app->signin, f, text);

    if (st != INDIGO_INPUT_OK) {
        indigo_app_set_status(&app->signin, indigo_input_status_message(st), true);
        return false;
    }
    indigo_app_set_status(&app->signin, "", false);
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

void
indigo_app_push_screen(indigo_app *app)
{
    if (app->history_count == sizeof app->history / sizeof app->history[0]) {
        memmove(&app->history[0], &app->history[1], sizeof app->history - sizeof app->history[0]);
        app->history_count--;
    }
    app->history[app->history_count++] = app->screen;
}

void
indigo_app_go_back(indigo_app *app)
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

void
indigo_app_toggle_like(indigo_app *app)
{
    const indigo_post *p = indigo_timeline_selected(indigo_app_active_list(app));

    if (!p || p->like_pending || app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    request_post_action(app, p->like_uri[0] ? INDIGO_REQUEST_UNLIKE : INDIGO_REQUEST_LIKE, p,
                        p->like_uri);
    indigo_app_set_like(app, p->uri, p->like_uri, true);
}

void
indigo_app_toggle_repost(indigo_app *app)
{
    const indigo_post *p = indigo_timeline_selected(indigo_app_active_list(app));

    if (!p || p->repost_pending || app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    request_post_action(app, p->repost_uri[0] ? INDIGO_REQUEST_UNREPOST : INDIGO_REQUEST_REPOST,
                        p, p->repost_uri);
    indigo_app_set_repost(app, p->uri, p->repost_uri, true);
}

void
indigo_app_refresh_timeline(indigo_app *app)
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

void
indigo_app_load_thread(indigo_app *app, const char *uri)
{
    indigo_copy_utf8(app->thread_uri, sizeof app->thread_uri, uri);
    indigo_copy_utf8(app->request_post_uri, sizeof app->request_post_uri, uri);
    indigo_timeline_begin_fetch(&app->thread, true);
    app->request = INDIGO_REQUEST_THREAD;
}

void
indigo_app_open_thread(indigo_app *app, const char *uri)
{
    if (!uri[0] || app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    indigo_app_push_screen(app);
    app->screen = INDIGO_SCREEN_THREAD;
    indigo_app_load_thread(app, uri);
}

void
indigo_app_open_profile(indigo_app *app, const char *actor)
{
    if (!actor[0] || app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    indigo_app_push_screen(app);
    app->screen = INDIGO_SCREEN_PROFILE;
    memset(&app->profile, 0, sizeof app->profile);
    app->profile.loading = true;
    indigo_copy_utf8(app->profile.handle, sizeof app->profile.handle, actor);
    indigo_copy_utf8(app->request_post_uri, sizeof app->request_post_uri, actor);
    app->request = INDIGO_REQUEST_PROFILE;
}

void
indigo_app_open_notifications(indigo_app *app)
{
    if (app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    indigo_app_push_screen(app);
    app->screen = INDIGO_SCREEN_NOTIFICATIONS;
    app->notifications.loading = true;
    app->notifications.status[0] = '\0';
    app->request = INDIGO_REQUEST_NOTIFICATIONS;
}

void
indigo_app_open_menu(indigo_app *app)
{
    indigo_app_push_screen(app);
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
    indigo_app_push_screen(app);
    app->screen = INDIGO_SCREEN_IMAGE;
    return true;
}

void
indigo_app_close_image(indigo_app *app)
{
    if (app->screen == INDIGO_SCREEN_IMAGE) {
        indigo_app_go_back(app);
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
        indigo_app_update_home(app, input);
        break;
    case INDIGO_SCREEN_THREAD:
        indigo_app_update_thread(app, input);
        break;
    case INDIGO_SCREEN_PROFILE:
        indigo_app_update_profile(app, input);
        break;
    case INDIGO_SCREEN_NOTIFICATIONS:
        indigo_app_update_notifications(app, input);
        break;
    case INDIGO_SCREEN_MENU:
        indigo_app_update_menu(app, input);
        break;
    case INDIGO_SCREEN_COMPOSE:
        indigo_app_update_compose(app, input);
        break;
    case INDIGO_SCREEN_SEARCH:
        indigo_app_update_search(app, input);
        break;
    case INDIGO_SCREEN_IMAGE:
        indigo_app_update_image(app, input);
        break;
    case INDIGO_SCREEN_SETTINGS:
        indigo_app_update_settings(app, input);
        break;
    case INDIGO_SCREEN_UPDATE:
        indigo_app_update_update(app, input);
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
