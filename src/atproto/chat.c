#include "atproto/chat.h"
#include "app/timeline.h"

#include <wolfram/chat_typed.h>

#include <stdio.h>
#include <string.h>

/* Members past this are not looked at; a convo with more is a group anyway. */
#define INDIGO_CHAT_MEMBERS_SCANNED 32

void
indigo_chat_inbox_reset(indigo_chat_inbox *inbox)
{
    if (inbox) {
        inbox->count = 0;
        inbox->unread = 0;
        inbox->cursor[0] = '\0';
        inbox->has_more = false;
    }
}

static void
keep_visible(unsigned *scroll, unsigned selected, unsigned rows)
{
    if (rows == 0) {
        return;
    }
    if (selected < *scroll) {
        *scroll = selected;
    } else if (selected >= *scroll + rows) {
        *scroll = selected + 1 - rows;
    }
}

void
indigo_chat_inbox_clear(indigo_chat_inbox *inbox)
{
    if (!inbox) {
        return;
    }
    indigo_chat_inbox_reset(inbox);
    inbox->selected = 0;
    inbox->scroll = 0;
    inbox->loading = false;
    inbox->status[0] = '\0';
    inbox->status_is_error = false;
}

bool
indigo_chat_inbox_select(indigo_chat_inbox *inbox, unsigned index, unsigned rows)
{
    if (!inbox || index >= inbox->count || index == inbox->selected) {
        return false;
    }
    inbox->selected = index;
    keep_visible(&inbox->scroll, index, rows);
    return true;
}

bool
indigo_chat_inbox_move(indigo_chat_inbox *inbox, int delta, unsigned rows)
{
    long target;

    if (!inbox || inbox->count == 0) {
        return false;
    }
    target = (long) inbox->selected + delta;
    if (target < 0) {
        target = 0;
    }
    if (target >= (long) inbox->count) {
        target = (long) inbox->count - 1;
    }
    return indigo_chat_inbox_select(inbox, (unsigned) target, rows);
}

static bool
is_other(const char *did, const char *viewer_did)
{
    if (!did || did[0] == '\0') {
        return false;
    }
    return !(viewer_did && strcmp(did, viewer_did) == 0);
}

int
indigo_chat_pick_counterpart(const char *const *member_dids, size_t count,
                             const char *viewer_did)
{
    if (!member_dids) {
        return -1;
    }
    for (size_t i = 0; i < count; i++) {
        if (is_other(member_dids[i], viewer_did)) {
            return (int)i;
        }
    }
    return -1;
}

int
indigo_chat_other_member_count(const char *const *member_dids, size_t count,
                               const char *viewer_did)
{
    int others = 0;

    if (!member_dids) {
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        if (is_other(member_dids[i], viewer_did)) {
            others++;
        }
    }
    return others;
}

int
indigo_chat_inbox_append_from_wolfram(indigo_chat_inbox *out,
                                      const struct wf_chat_convo_list *list,
                                      const char *viewer_did)
{
    const wf_chat_convo_list *typed = (const wf_chat_convo_list *)list;
    int added = 0;

    if (!out || !typed) {
        return 0;
    }

    for (size_t i = 0; i < typed->convo_count; i++) {
        if (out->count >= INDIGO_CHAT_CONVOS_MAX) {
            break;
        }

        const wf_chat_convo *src = &typed->convos[i];
        const size_t scanned = src->member_count < INDIGO_CHAT_MEMBERS_SCANNED
                                   ? src->member_count
                                   : INDIGO_CHAT_MEMBERS_SCANNED;
        const char *dids[INDIGO_CHAT_MEMBERS_SCANNED];
        for (size_t m = 0; m < scanned; m++) {
            dids[m] = src->members[m].did;
        }

        int others = indigo_chat_other_member_count(dids, scanned, viewer_did);
        if (src->member_count > INDIGO_CHAT_MEMBERS_SCANNED && others < 2) {
            others = 2;
        }
        if (others == 0) {
            continue; /* only the viewer: nothing to show */
        }

        indigo_chat_convo *item = &out->items[out->count];
        memset(item, 0, sizeof *item);
        snprintf(item->id, sizeof item->id, "%s", src->id ? src->id : "");

        if (others > 1) {
            item->group = true;
            snprintf(item->name, sizeof item->name, "Group");
        } else {
            const int at = indigo_chat_pick_counterpart(dids, scanned, viewer_did);
            const wf_agent_profile_view *who = at >= 0 ? &src->members[at] : NULL;
            const char *handle = who && who->handle ? who->handle : "";
            const char *display = who && who->display_name ? who->display_name : "";
            if (display[0] == '\0') {
                display = handle;
            }
            indigo_copy_utf8(item->name, sizeof item->name, display);
            snprintf(item->handle, sizeof item->handle, "@%s", handle);
        }

        if (src->last_message_text) {
            indigo_copy_utf8(item->preview, sizeof item->preview, src->last_message_text);
        }

        item->unread = src->has_unread_count && src->unread_count > 0 ? src->unread_count : 0;
        out->unread += item->unread;
        out->count++;
        added++;
    }

    /* A full inbox means there may be more, but the screen has nowhere to put
     * them, so it stops asking. Otherwise keep the server's cursor. */
    if (out->count >= INDIGO_CHAT_CONVOS_MAX || !typed->cursor || !typed->cursor[0]) {
        out->cursor[0] = '\0';
        out->has_more = false;
    } else {
        indigo_copy_utf8(out->cursor, sizeof out->cursor, typed->cursor);
        out->has_more = true;
    }
    return added;
}
