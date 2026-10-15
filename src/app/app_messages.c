/* Input on the direct message inbox. Read-only: a conversation cannot be opened
 * yet, because its message view is not built, so confirm does nothing. */

#include "app/app_internal.h"

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
        case INDIGO_ACTION_REFRESH:
            request_refresh(app);
            break;
        case INDIGO_ACTION_BACK:
            indigo_app_go_back(app);
            break;
        default:
            break;
        }
    }
}
