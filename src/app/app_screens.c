/* Input on the menu, search, compose, settings, update and image screens. */

#include "app/app_internal.h"

/* Search keeps its query and results across visits: retyping a name to reach
 * the same list again would be the wrong trade on a system keyboard. */
static void
/* The kind is set by the caller because the menu offers both a person and a
 * post search. Either way it is set explicitly: inheriting the previous
 * list's kind would show that list's results under the wrong heading. */
open_search(indigo_app *app, indigo_search_kind kind)
{
    indigo_app_push_screen(app);
    app->screen = INDIGO_SCREEN_SEARCH;
    app->search.kind = kind;
    app->search.loading = false;
}

void
indigo_app_submit_search(indigo_app *app)
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
void
indigo_app_begin_compose(indigo_app *app, indigo_compose_mode mode, const indigo_post *target)
{
    indigo_compose *c = &app->compose;

    if (c->sending) {
        return;
    }
    indigo_app_push_screen(app);
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

/* The attach control: with an image attached it removes it, otherwise it opens
 * the picker, which is the menu screen filled with the folder's pictures. */
static void
attach_or_detach(indigo_app *app)
{
    indigo_compose *c = &app->compose;

    if (c->sending) {
        return;
    }
    if (indigo_compose_has_image(c)) {
        indigo_compose_clear_image(c);
        return;
    }
    indigo_app_push_screen(app);
    indigo_menu_build_images(&app->menu, app->images_dir);
    app->screen = INDIGO_SCREEN_MENU;
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
        indigo_app_go_back(app);
        indigo_app_open_profile(app, it->payload);
        break;
    case INDIGO_MENU_SHOW_TAG:
    case INDIGO_MENU_SHOW_LINK:
        indigo_app_go_back(app);
        say(app, it->label);
        break;
    case INDIGO_MENU_LIKED_BY:
    case INDIGO_MENU_REPOSTED_BY: {
        char uri[INDIGO_POST_URI_MAX];

        /* Copied first: go_back leaves the menu screen and the list is about
         * to be rebuilt from the post, not the menu. The list is pushed so B
         * returns to the timeline the post is on. */
        indigo_copy_utf8(uri, sizeof uri, app->menu.post_uri);
        indigo_app_go_back(app);
        indigo_app_push_screen(app);
        indigo_app_open_people(app,
                               it->kind == INDIGO_MENU_LIKED_BY ? INDIGO_SEARCH_LIKED_BY
                                                                : INDIGO_SEARCH_REPOSTED_BY,
                               uri);
        break;
    }
    case INDIGO_MENU_COMPOSE:
        indigo_app_go_back(app);
        indigo_app_begin_compose(app, INDIGO_COMPOSE_POST, NULL);
        break;
    case INDIGO_MENU_NOTIFICATIONS:
        indigo_app_go_back(app);
        indigo_app_open_notifications(app);
        break;
    case INDIGO_MENU_FIND_PEOPLE:
        indigo_app_go_back(app);
        open_search(app, INDIGO_SEARCH_PEOPLE);
        break;
    case INDIGO_MENU_FIND_POSTS:
        indigo_app_go_back(app);
        open_search(app, INDIGO_SEARCH_POSTS);
        break;
    case INDIGO_MENU_LISTS:
        indigo_app_go_back(app);
        indigo_app_open_lists(app);
        break;
    case INDIGO_MENU_FEEDS:
        indigo_app_go_back(app);
        indigo_app_open_feeds(app);
        break;
    case INDIGO_MENU_MUTED:
        indigo_app_go_back(app);
        indigo_app_open_mutes(app);
        break;
    case INDIGO_MENU_BLOCKED:
        indigo_app_go_back(app);
        indigo_app_open_blocks(app);
        break;
    case INDIGO_MENU_MY_PROFILE:
        indigo_app_go_back(app);
        indigo_app_open_profile(app, app->signin.account);
        break;
    case INDIGO_MENU_SETTINGS:
        indigo_app_go_back(app);
        indigo_app_open_settings(app);
        break;
    case INDIGO_MENU_UPDATE:
        indigo_app_go_back(app);
        indigo_app_open_update(app);
        break;
    case INDIGO_MENU_SIGN_OUT:
        app->request = INDIGO_REQUEST_SIGN_OUT;
        break;
    case INDIGO_MENU_PICK_IMAGE:
        indigo_app_go_back(app);
        /* A path that does not fit is dropped rather than cut short into a
         * different file. */
        if (snprintf(app->compose.image, sizeof app->compose.image, "%s/%s", app->images_dir,
                     it->payload) >= (int) sizeof app->compose.image) {
            app->compose.image[0] = '\0';
            break;
        }
        app->compose.image_alt[0] = '\0';
        /* Alt text is asked for straight away, while the picture is the thing
         * being thought about. Cancelling the keyboard leaves it empty. */
        app->request = INDIGO_REQUEST_EDIT_IMAGE_ALT;
        break;
    case INDIGO_MENU_CLOSE:
        indigo_app_go_back(app);
        break;
    }
}

void
indigo_app_update_menu(indigo_app *app, const indigo_input *input)
{
    int drag = indigo_app_drag_rows(app, input);

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
        indigo_app_go_back(app);
        return;
    }
    if (input->confirm) {
        menu_choose(app, app->menu.selected);
        return;
    }
    if (input->touch_pressed) {
        indigo_action a = indigo_layout_hit_app(app, input->touch_x, input->touch_y);

        if (a == INDIGO_ACTION_BACK) {
            indigo_app_go_back(app);
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
            indigo_app_open_thread(app, p->uri);
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
            indigo_app_open_profile(app, a->handle);
        }
    } else {
        const indigo_actor *a = indigo_search_selected(s);

        if (a) {
            indigo_app_open_profile(app, a->handle);
        }
    }
}

void
indigo_app_update_search(indigo_app *app, const indigo_input *input)
{
    indigo_search *s = &app->search;
    int drag = indigo_app_drag_rows(app, input);

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
        indigo_app_go_back(app);
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
            indigo_app_go_back(app);
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

void
indigo_app_update_compose(indigo_app *app, const indigo_input *input)
{
    indigo_compose *c = &app->compose;

    if (c->sending) {
        return;
    }
    if (input->back) {
        indigo_app_go_back(app);
        return;
    }
    if (input->confirm && app->request == INDIGO_REQUEST_NONE) {
        app->request = INDIGO_REQUEST_EDIT_DRAFT;
    }
    if (input->like) {
        compose_second_button(app);
    }
    if (input->refresh) {
        attach_or_detach(app);
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
        case INDIGO_ACTION_ATTACH:
            attach_or_detach(app);
            break;
        case INDIGO_ACTION_SEND:
            send_compose(app);
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

void
indigo_app_update_settings(indigo_app *app, const indigo_input *input)
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
        indigo_app_go_back(app);
        return;
    }
    if (input->confirm) {
        toggle_setting_row(app, app->settings_selected);
        return;
    }
    if (input->touch_pressed) {
        indigo_action a = indigo_layout_hit_app(app, input->touch_x, input->touch_y);
        if (a == INDIGO_ACTION_BACK) {
            indigo_app_go_back(app);
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

void
indigo_app_update_update(indigo_app *app, const indigo_input *input)
{
    if (input->back) {
        indigo_app_go_back(app);
        return;
    }
    if (input->confirm) {
        press_update(app);
        return;
    }
    if (input->touch_pressed) {
        indigo_action a = indigo_layout_hit_app(app, input->touch_x, input->touch_y);

        if (a == INDIGO_ACTION_BACK) {
            indigo_app_go_back(app);
        } else if (a == INDIGO_ACTION_UPDATE) {
            press_update(app);
        }
    }
}

/* The viewer has no state of its own to change: B and the Close button both
 * leave, and nothing on either screen can be pressed. ZR is the shortcut for
 * the same thing, because it is the shortcut that opened it. */
void
indigo_app_update_image(indigo_app *app, const indigo_input *input)
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
