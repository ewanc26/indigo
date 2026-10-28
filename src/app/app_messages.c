/* Input on the direct message screens: the inbox, a conversation, message
 * requests and a group's join requests. */

#include "app/app_internal.h"

static bool
decision_busy(const indigo_app *app)
{
    return app->chat_decision_pending || app->request != INDIGO_REQUEST_NONE;
}

static void
begin_request_decision(indigo_app *app, const indigo_chat_convo *row, bool accept)
{
    indigo_copy_utf8(app->chat_decision_id, sizeof app->chat_decision_id, row->id);
    indigo_copy_utf8(app->chat_decision_name, sizeof app->chat_decision_name,
                     row->name[0] ? row->name : row->handle);
    app->chat_decision_accept = accept;
    app->chat_decision_pending = true;
    app->requests.status[0] = '\0';
    app->requests.status_is_error = false;
    app->request = accept ? INDIGO_REQUEST_CHAT_ACCEPT : INDIGO_REQUEST_CHAT_DECLINE;
}

static void
begin_join_decision(indigo_app *app, const indigo_chat_join_request *row, bool approve)
{
    indigo_copy_utf8(app->chat_decision_id, sizeof app->chat_decision_id, row->did);
    indigo_copy_utf8(app->chat_decision_name, sizeof app->chat_decision_name,
                     row->name[0] ? row->name : row->handle);
    app->chat_decision_accept = approve;
    app->chat_decision_pending = true;
    app->join_requests.status[0] = '\0';
    app->join_requests.status_is_error = false;
    app->request = approve ? INDIGO_REQUEST_CHAT_JOIN_APPROVE
                           : INDIGO_REQUEST_CHAT_JOIN_REJECT;
}

static void
request_more(indigo_app *app)
{
    indigo_chat_inbox *r = &app->requests;

    if (!r->loading && r->has_more && app->request == INDIGO_REQUEST_NONE) {
        r->loading = true;
        app->request = INDIGO_REQUEST_CHAT_REQUESTS_MORE;
    }
}

static void
request_reload(indigo_app *app)
{
    indigo_chat_inbox *r = &app->requests;

    if (!r->loading && app->request == INDIGO_REQUEST_NONE) {
        r->loading = true;
        app->request = INDIGO_REQUEST_CHAT_REQUESTS;
    }
}

void
indigo_app_open_requests(indigo_app *app)
{
    if (app->request != INDIGO_REQUEST_NONE) {
        return;
    }
    indigo_app_push_screen(app);
    app->screen = INDIGO_SCREEN_CHAT_REQUESTS;
    indigo_chat_inbox_clear(&app->requests);
    app->requests.loading = true;
    app->request = INDIGO_REQUEST_CHAT_REQUESTS;
}

void
indigo_app_open_join_requests(indigo_app *app)
{
    if (app->request != INDIGO_REQUEST_NONE || !app->chat_is_group ||
        !app->request_post_uri[0]) {
        return;
    }
    indigo_app_push_screen(app);
    app->screen = INDIGO_SCREEN_CHAT_JOIN_REQUESTS;
    indigo_chat_join_requests_clear(&app->join_requests);
    indigo_copy_utf8(app->join_requests.convo_id, sizeof app->join_requests.convo_id,
                     app->request_post_uri);
    app->join_requests.loading = true;
    app->request = INDIGO_REQUEST_CHAT_JOIN_REQUESTS;
}

static void
join_requests_more(indigo_app *app)
{
    indigo_chat_join_requests *l = &app->join_requests;

    if (!l->loading && l->has_more && app->request == INDIGO_REQUEST_NONE) {
        l->loading = true;
        app->request = INDIGO_REQUEST_CHAT_JOIN_REQUESTS_MORE;
    }
}

static void
join_requests_reload(indigo_app *app)
{
    indigo_chat_join_requests *l = &app->join_requests;

    if (!l->loading && app->request == INDIGO_REQUEST_NONE) {
        l->loading = true;
        app->request = INDIGO_REQUEST_CHAT_JOIN_REQUESTS;
    }
}

/* The request list. A accepts the highlighted request and X declines it; both
 * are one press and one request, never sent on entry or retried. */
void
indigo_app_update_chat_requests(indigo_app *app, const indigo_input *input)
{
    indigo_chat_inbox *r = &app->requests;
    int drag = indigo_app_drag_rows(app, input);

    if (input->up) indigo_chat_inbox_move(r, -1, INDIGO_TIMELINE_ROWS);
    if (input->down) indigo_chat_inbox_move(r, 1, INDIGO_TIMELINE_ROWS);
    if (input->page_up) indigo_chat_inbox_move(r, -INDIGO_TIMELINE_ROWS, INDIGO_TIMELINE_ROWS);
    if (input->page_down) indigo_chat_inbox_move(r, INDIGO_TIMELINE_ROWS, INDIGO_TIMELINE_ROWS);
    if (drag != 0) indigo_chat_inbox_move(r, drag, INDIGO_TIMELINE_ROWS);
    if (r->count > 0 && r->selected + 1 >= r->count) {
        request_more(app);
    }
    if (input->back) {
        indigo_app_go_back(app);
        return;
    }
    if (input->refresh) {
        request_reload(app);
    }
    if (!decision_busy(app) && r->selected < r->count) {
        if (input->confirm) {
            begin_request_decision(app, &r->items[r->selected], true);
            return;
        }
        if (input->repost) {
            begin_request_decision(app, &r->items[r->selected], false);
            return;
        }
    }
    if (input->touch_pressed) {
        indigo_action a = indigo_layout_hit_app(app, input->touch_x, input->touch_y);

        switch (a) {
        case INDIGO_ACTION_ROW0:
        case INDIGO_ACTION_ROW1:
        case INDIGO_ACTION_ROW2:
            indigo_chat_inbox_select(r, r->scroll + (unsigned) (a - INDIGO_ACTION_ROW0),
                                     INDIGO_TIMELINE_ROWS);
            break;
        case INDIGO_ACTION_ACCEPT:
            if (!decision_busy(app) && r->selected < r->count) {
                begin_request_decision(app, &r->items[r->selected], true);
            }
            break;
        case INDIGO_ACTION_DECLINE:
            if (!decision_busy(app) && r->selected < r->count) {
                begin_request_decision(app, &r->items[r->selected], false);
            }
            break;
        case INDIGO_ACTION_REFRESH:
            request_reload(app);
            break;
        case INDIGO_ACTION_BACK:
            indigo_app_go_back(app);
            break;
        default:
            break;
        }
    }
}

/* The join requests of the group this conversation belongs to. A approves and
 * X rejects; only the group's owner can list them, and the server refuses
 * anyone else. */
void
indigo_app_update_chat_join_requests(indigo_app *app, const indigo_input *input)
{
    indigo_chat_join_requests *l = &app->join_requests;
    int drag = indigo_app_drag_rows(app, input);

    if (input->up) indigo_chat_join_requests_move(l, -1, INDIGO_TIMELINE_ROWS);
    if (input->down) indigo_chat_join_requests_move(l, 1, INDIGO_TIMELINE_ROWS);
    if (input->page_up)
        indigo_chat_join_requests_move(l, -INDIGO_TIMELINE_ROWS, INDIGO_TIMELINE_ROWS);
    if (input->page_down)
        indigo_chat_join_requests_move(l, INDIGO_TIMELINE_ROWS, INDIGO_TIMELINE_ROWS);
    if (drag != 0) indigo_chat_join_requests_move(l, drag, INDIGO_TIMELINE_ROWS);
    if (l->count > 0 && l->selected + 1 >= l->count) {
        join_requests_more(app);
    }
    if (input->back) {
        indigo_app_go_back(app);
        return;
    }
    if (input->refresh) {
        join_requests_reload(app);
    }
    if (!decision_busy(app) && l->selected < l->count) {
        if (input->confirm) {
            begin_join_decision(app, &l->items[l->selected], true);
            return;
        }
        if (input->repost) {
            begin_join_decision(app, &l->items[l->selected], false);
            return;
        }
    }
    if (input->touch_pressed) {
        indigo_action a = indigo_layout_hit_app(app, input->touch_x, input->touch_y);

        switch (a) {
        case INDIGO_ACTION_ROW0:
        case INDIGO_ACTION_ROW1:
        case INDIGO_ACTION_ROW2:
            indigo_chat_join_requests_select(l, l->scroll + (unsigned) (a - INDIGO_ACTION_ROW0),
                                             INDIGO_TIMELINE_ROWS);
            break;
        case INDIGO_ACTION_APPROVE:
            if (!decision_busy(app) && l->selected < l->count) {
                begin_join_decision(app, &l->items[l->selected], true);
            }
            break;
        case INDIGO_ACTION_REJECT:
            if (!decision_busy(app) && l->selected < l->count) {
                begin_join_decision(app, &l->items[l->selected], false);
            }
            break;
        case INDIGO_ACTION_REFRESH:
            join_requests_reload(app);
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
request_refresh(indigo_app *app)
{
    indigo_chat_inbox *m = &app->messages;

    if (!m->loading && app->request == INDIGO_REQUEST_NONE) {
        m->loading = true;
        app->request = INDIGO_REQUEST_CHAT_INBOX;
    }
}

void
indigo_app_update_messages(indigo_app *app, const indigo_input *input)
{
    indigo_chat_inbox *m = &app->messages;
    int drag = indigo_app_drag_rows(app, input);

    if (input->up) {
        indigo_chat_inbox_move(m, -1, INDIGO_TIMELINE_ROWS);
    }
    if (input->down) {
        indigo_chat_inbox_move(m, 1, INDIGO_TIMELINE_ROWS);
    }
    if (input->page_up) {
        indigo_chat_inbox_move(m, -INDIGO_TIMELINE_ROWS, INDIGO_TIMELINE_ROWS);
    }
    if (input->page_down) {
        indigo_chat_inbox_move(m, INDIGO_TIMELINE_ROWS, INDIGO_TIMELINE_ROWS);
    }
    if (drag != 0) {
        indigo_chat_inbox_move(m, drag, INDIGO_TIMELINE_ROWS);
    }
    if (input->back) {
        indigo_app_go_back(app);
        return;
    }
    if (input->confirm && m->selected < m->count) {
        const indigo_chat_convo *c = &m->items[m->selected];

        indigo_app_open_chat_thread(app, c->id, c->name[0] ? c->name : c->handle,
                                    c->group);
        return;
    }
    /* X starts a new conversation; Y opens the request list. */
    if (input->repost && app->request == INDIGO_REQUEST_NONE) {
        app->request = INDIGO_REQUEST_EDIT_CHAT_TARGET;
    }
    if (input->like) {
        indigo_app_open_requests(app);
    }
    if (input->refresh) {
        request_refresh(app);
    }
    if (input->touch_pressed) {
        indigo_action a = indigo_layout_hit_app(app, input->touch_x, input->touch_y);

        switch (a) {
        case INDIGO_ACTION_ROW0:
        case INDIGO_ACTION_ROW1:
        case INDIGO_ACTION_ROW2:
            indigo_chat_inbox_select(m, m->scroll + (unsigned) (a - INDIGO_ACTION_ROW0),
                                     INDIGO_TIMELINE_ROWS);
            break;
        case INDIGO_ACTION_NEW_MESSAGE:
            if (app->request == INDIGO_REQUEST_NONE) {
                app->request = INDIGO_REQUEST_EDIT_CHAT_TARGET;
            }
            break;
        case INDIGO_ACTION_REFRESH:
            request_refresh(app);
            break;
        case INDIGO_ACTION_REQUESTS:
            indigo_app_open_requests(app);
            break;
        case INDIGO_ACTION_BACK:
            indigo_app_go_back(app);
            break;
        default:
            break;
        }
    }
}

/* Input on one conversation. Up and Down step through the messages, A writes a
 * reply and Y sends it, and X opens the join requests of a group. */
void
indigo_app_update_chat_thread(indigo_app *app, const indigo_input *input)
{
    indigo_chat_thread *t = &app->chat_thread;
    int drag = indigo_app_drag_rows(app, input);

    if (input->up) {
        indigo_chat_thread_move(t, -1, INDIGO_CHAT_THREAD_ROWS);
    }
    if (input->down) {
        indigo_chat_thread_move(t, 1, INDIGO_CHAT_THREAD_ROWS);
    }
    if (drag != 0) {
        indigo_chat_thread_move(t, drag, INDIGO_CHAT_THREAD_ROWS);
    }
    if (input->back) {
        indigo_app_go_back(app);
        return;
    }
    /* A opens the keyboard on the reply; Y sends it. A send that fails keeps the reply. */
    if (input->confirm && !t->sending && app->request == INDIGO_REQUEST_NONE) {
        app->request = INDIGO_REQUEST_EDIT_REPLY;
    }
    if (input->like && !t->sending && !t->loading && app->request == INDIGO_REQUEST_NONE &&
        indigo_chat_reply_valid(t->reply)) {
        t->sending = true;
        t->status[0] = '\0';
        app->request = INDIGO_REQUEST_CHAT_SEND;
    }
    if (input->repost && app->chat_is_group) {
        indigo_app_open_join_requests(app);
    }
    if (input->refresh && !t->loading && app->request == INDIGO_REQUEST_NONE) {
        t->loading = true;
        app->request = INDIGO_REQUEST_CHAT_THREAD;
    }
    if (input->touch_pressed) {
        indigo_action a = indigo_layout_hit_app(app, input->touch_x, input->touch_y);

        switch (a) {
        case INDIGO_ACTION_ROW0:
        case INDIGO_ACTION_ROW1:
        case INDIGO_ACTION_ROW2:
            indigo_chat_thread_select(t, t->scroll + (unsigned) (a - INDIGO_ACTION_ROW0),
                                      INDIGO_CHAT_THREAD_ROWS);
            break;
        case INDIGO_ACTION_JOIN_REQUESTS:
            if (app->chat_is_group) {
                indigo_app_open_join_requests(app);
            }
            break;
        case INDIGO_ACTION_REFRESH:
            if (!t->loading && app->request == INDIGO_REQUEST_NONE) {
                t->loading = true;
                app->request = INDIGO_REQUEST_CHAT_THREAD;
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
indigo_app_set_chat_target(indigo_app *app, const char *text)
{
    char target[INDIGO_CHAT_TARGET_MAX];

    if (!indigo_chat_target_normalise(text, target, sizeof target) ||
        indigo_chat_target_kind_of(target) == INDIGO_CHAT_TARGET_INVALID) {
        indigo_copy_utf8(app->messages.status, sizeof app->messages.status,
                         "Enter a handle or a DID to message.");
        app->messages.status_is_error = true;
        app->chat_start_pending = false;
        return;
    }
    indigo_copy_utf8(app->chat_target, sizeof app->chat_target, target);
    app->chat_start_pending = true;
    indigo_copy_utf8(app->messages.status, sizeof app->messages.status, "Starting...");
    app->messages.status_is_error = false;
    app->request = INDIGO_REQUEST_CHAT_START;
}

void
indigo_app_chat_start_done(indigo_app *app, const char *convo_id)
{
    char id[INDIGO_CHAT_ID_MAX];
    char title[INDIGO_POST_NAME_MAX];

    if (!app->chat_start_pending) {
        return;
    }
    app->chat_start_pending = false;
    if (!indigo_chat_convo_id_copy(id, sizeof id, convo_id)) {
        indigo_app_chat_start_failed(app, "The server did not return a conversation.");
        return;
    }
    if (app->request != INDIGO_REQUEST_NONE) {
        indigo_copy_utf8(app->messages.status, sizeof app->messages.status,
                         "The conversation was created; open Messages again.");
        app->messages.status_is_error = true;
        return;
    }
    indigo_copy_utf8(title, sizeof title, app->chat_target);
    app->chat_target[0] = '\0';
    app->messages.status[0] = '\0';
    app->messages.status_is_error = false;
    indigo_app_open_chat_thread(app, id, title, false);
}

void
indigo_app_chat_start_failed(indigo_app *app, const char *message)
{
    app->chat_start_pending = false;
    indigo_copy_utf8(app->messages.status, sizeof app->messages.status, message);
    app->messages.status_is_error = true;
}

void
indigo_app_chat_request_done(indigo_app *app, bool accepted)
{
    indigo_chat_convo moved;
    const indigo_chat_convo *row;
    bool had = false;

    if (!app->chat_decision_pending) {
        return;
    }
    app->chat_decision_pending = false;

    row = indigo_chat_inbox_find(&app->requests, app->chat_decision_id);
    if (row) {
        moved = *row;
        had = true;
    }
    indigo_chat_inbox_remove(&app->requests, app->chat_decision_id);
    if (accepted && had && app->messages.count > 0) {
        indigo_chat_inbox_add_front(&app->messages, &moved);
    }
    app->requests_count = (int) app->requests.count;
    if (app->requests.selected >= app->requests.count && app->requests.count > 0) {
        app->requests.selected = app->requests.count - 1;
    }
    indigo_copy_utf8(app->requests.status, sizeof app->requests.status,
                     accepted ? "Request accepted." : "Request declined.");
    app->requests.status_is_error = false;
}

void
indigo_app_chat_request_failed(indigo_app *app, const char *message)
{
    app->chat_decision_pending = false;
    indigo_copy_utf8(app->requests.status, sizeof app->requests.status, message);
    app->requests.status_is_error = true;
}

void
indigo_app_join_requests_loaded(indigo_app *app, const indigo_chat_join_requests *src)
{
    indigo_chat_join_requests *l = &app->join_requests;

    if (src) {
        l->count = src->count;
        l->has_more = src->has_more;
        memcpy(l->cursor, src->cursor, sizeof l->cursor);
        memcpy(l->items, src->items, sizeof l->items);
    }
    l->loading = false;
    l->status[0] = '\0';
    l->status_is_error = false;
    if (l->count == 0) {
        l->selected = 0;
        l->scroll = 0;
        indigo_copy_utf8(l->status, sizeof l->status, "No join requests.");
    } else if (l->selected >= l->count) {
        l->selected = l->count - 1;
    }
    if (l->scroll > l->selected) {
        l->scroll = l->selected;
    }
}

void
indigo_app_join_requests_failed(indigo_app *app, const char *message)
{
    app->join_requests.loading = false;
    indigo_copy_utf8(app->join_requests.status, sizeof app->join_requests.status, message);
    app->join_requests.status_is_error = true;
}

void
indigo_app_chat_join_decision_done(indigo_app *app, bool approved)
{
    if (!app->chat_decision_pending) {
        return;
    }
    app->chat_decision_pending = false;
    indigo_chat_join_requests_remove(&app->join_requests, app->chat_decision_id);
    if (app->join_requests.selected >= app->join_requests.count &&
        app->join_requests.count > 0) {
        app->join_requests.selected = app->join_requests.count - 1;
    }
    indigo_copy_utf8(app->join_requests.status, sizeof app->join_requests.status,
                     approved ? "Request approved." : "Request rejected.");
    app->join_requests.status_is_error = false;
}

void
indigo_app_chat_join_decision_failed(indigo_app *app, const char *message)
{
    app->chat_decision_pending = false;
    indigo_copy_utf8(app->join_requests.status, sizeof app->join_requests.status, message);
    app->join_requests.status_is_error = true;
}
