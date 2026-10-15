#include "app/dms.h"

#include <string.h>

void
indigo_dms_clear_list(indigo_dms *d)
{
    d->count = 0;
    d->selected = 0;
    d->scroll = 0;
    d->cursor[0] = '\0';
    d->has_more = false;
    d->loading = false;
    d->status[0] = '\0';
    d->status_is_error = false;
}

void
indigo_dms_clear_messages(indigo_dms *d)
{
    d->message_count = 0;
    d->scroll_msg = 0;
    d->loading_messages = false;
    d->status_msg[0] = '\0';
    d->status_msg_is_error = false;
}

bool
indigo_dms_append_convo(indigo_dms *d, const indigo_convo *c)
{
    if (d->count >= INDIGO_CONVO_MAX) {
        return false;
    }
    d->convos[d->count++] = *c;
    return true;
}

bool
indigo_dms_append_message(indigo_dms *d, const indigo_message *m)
{
    if (d->message_count >= INDIGO_MESSAGE_MAX) {
        return false;
    }
    d->messages[d->message_count++] = *m;
    return true;
}

const indigo_convo *
indigo_dms_selected(const indigo_dms *d)
{
    if (d->active_id[0]) {
        return NULL;
    }
    return d->selected < d->count ? &d->convos[d->selected] : NULL;
}

const indigo_convo *
indigo_dms_find(const indigo_dms *d, const char *id)
{
    for (unsigned i = 0; i < d->count; i++) {
        if (strcmp(d->convos[i].id, id) == 0) {
            return &d->convos[i];
        }
    }
    return NULL;
}

bool
indigo_dms_select_convo(indigo_dms *d, unsigned index)
{
    if (index >= d->count || index == d->selected) {
        return false;
    }
    d->selected = index;
    d->scroll = d->selected;
    return true;
}

bool
indigo_dms_move_convo(indigo_dms *d, int delta, unsigned rows)
{
    long target = (long) d->selected + delta;

    if (d->count == 0) {
        return false;
    }
    if (target < 0) {
        target = 0;
    }
    if (target >= (long) d->count) {
        target = (long) d->count - 1;
    }
    d->selected = (unsigned) target;
    if (d->selected >= d->scroll + rows) {
        d->scroll = d->selected - rows + 1;
    }
    if (d->selected < d->scroll) {
        d->scroll = d->selected;
    }
    return true;
}

bool
indigo_dms_wants_page(const indigo_dms *d)
{
    if (d->loading || !d->has_more) {
        return false;
    }
    if (d->active_id[0]) {
        return d->message_count < INDIGO_MESSAGE_MAX;
    }
    return d->count < INDIGO_CONVO_MAX;
}
