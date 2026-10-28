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
indigo_chat_inbox_can_page(const indigo_chat_inbox *inbox)
{
    return inbox && inbox->has_more && inbox->count < INDIGO_CHAT_CONVOS_MAX;
}

const indigo_chat_convo *
indigo_chat_inbox_find(const indigo_chat_inbox *inbox, const char *convo_id)
{
    if (!inbox || !convo_id) {
        return NULL;
    }
    for (unsigned i = 0; i < inbox->count && i < INDIGO_CHAT_CONVOS_MAX; i++) {
        if (strcmp(inbox->items[i].id, convo_id) == 0) {
            return &inbox->items[i];
        }
    }
    return NULL;
}

bool
indigo_chat_inbox_remove(indigo_chat_inbox *inbox, const char *convo_id)
{
    if (!inbox || !convo_id) {
        return false;
    }
    for (unsigned i = 0; i < inbox->count && i < INDIGO_CHAT_CONVOS_MAX; i++) {
        if (strcmp(inbox->items[i].id, convo_id) != 0) {
            continue;
        }
        const int unread = inbox->items[i].unread > 0 ? inbox->items[i].unread : 0;
        inbox->unread = inbox->unread > unread ? inbox->unread - unread : 0;
        memmove(&inbox->items[i], &inbox->items[i + 1],
                (size_t) (inbox->count - i - 1) * sizeof(inbox->items[0]));
        inbox->count--;
        return true;
    }
    return false;
}

bool
indigo_chat_inbox_add_front(indigo_chat_inbox *inbox, const indigo_chat_convo *item)
{
    unsigned n;

    if (!inbox || !item || indigo_chat_inbox_find(inbox, item->id)) {
        return false;
    }
    n = inbox->count < INDIGO_CHAT_CONVOS_MAX ? inbox->count : INDIGO_CHAT_CONVOS_MAX - 1;
    if (n < inbox->count) {
        /* Full: the last row goes, and its unread messages leave the total. */
        const int dropped = inbox->items[n].unread > 0 ? inbox->items[n].unread : 0;
        inbox->unread = inbox->unread > dropped ? inbox->unread - dropped : 0;
    }
    memmove(&inbox->items[1], &inbox->items[0], (size_t) n * sizeof(inbox->items[0]));
    inbox->items[0] = *item;
    inbox->count = n + 1;
    if (item->unread > 0) {
        inbox->unread += item->unread;
    }
    return true;
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

bool
indigo_chat_requests_label(int count, char *out, size_t cap)
{
    int written;

    if (!out || cap == 0) {
        return false;
    }
    if (count <= 0) {
        written = snprintf(out, cap, "No requests");
    } else if (count == 1) {
        written = snprintf(out, cap, "1 request");
    } else {
        written = snprintf(out, cap, "%d requests", count);
    }
    if (written < 0 || (size_t) written >= cap) {
        out[0] = '\0';
        return false;
    }
    return true;
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

static bool
chat_is_alnum(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

/* did:<method>:<id>, with a lowercase method and an id of DID characters. */
static bool
chat_did_valid(const char *s)
{
    const char *p;
    const char *method;
    const char *id;

    if (!s || strncmp(s, "did:", 4) != 0) {
        return false;
    }
    p = s + 4;
    method = p;
    while (*p >= 'a' && *p <= 'z') {
        p++;
    }
    if (p == method || *p != ':') {
        return false;
    }
    id = ++p;
    for (; *p; p++) {
        const char c = *p;
        const bool ok = chat_is_alnum(c) || c == '.' || c == '_' || c == ':' ||
                        c == '%' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return p > id && p[-1] != ':';
}

/* atproto handle syntax: labels of letters, digits and hyphens, each 1 to 63
 * bytes and not starting or ending with a hyphen, at most 253 bytes in all,
 * at least two labels, and a last label that starts with a letter. */
static bool
chat_handle_valid(const char *s)
{
    size_t total;
    int labels = 0;
    const char *label;

    if (!s) {
        return false;
    }
    total = strlen(s);
    if (total == 0 || total > 253) {
        return false;
    }
    label = s;
    for (const char *p = s;; p++) {
        if (*p == '.' || *p == '\0') {
            const size_t n = (size_t) (p - label);
            if (n == 0 || n > 63 || label[0] == '-' || p[-1] == '-') {
                return false;
            }
            for (const char *q = label; q < p; q++) {
                if (!chat_is_alnum(*q) && *q != '-') {
                    return false;
                }
            }
            labels++;
            if (*p == '\0') {
                break;
            }
            label = p + 1;
        }
    }
    if (labels < 2) {
        return false;
    }
    {
        const char tld = strrchr(s, '.')[1];
        return (tld >= 'a' && tld <= 'z') || (tld >= 'A' && tld <= 'Z');
    }
}

bool
indigo_chat_target_normalise(const char *input, char *out, size_t cap)
{
    const char *start;
    const char *end;
    size_t len;

    if (!input || !out || cap == 0) {
        return false;
    }
    out[0] = '\0';

    start = input;
    end = input + strlen(input);
    while (start < end && (*start == ' ' || *start == '\t' || *start == '\n' ||
                           *start == '\r')) {
        start++;
    }
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' ||
                           end[-1] == '\r')) {
        end--;
    }
    if (start < end && *start == '@') {
        start++;
    }

    len = (size_t) (end - start);
    if (len == 0 || len >= cap) {
        return false;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

indigo_chat_target_kind
indigo_chat_target_kind_of(const char *target)
{
    if (!target || target[0] == '\0' || strlen(target) >= INDIGO_CHAT_TARGET_MAX) {
        return INDIGO_CHAT_TARGET_INVALID;
    }
    if (chat_did_valid(target)) {
        return INDIGO_CHAT_TARGET_DID;
    }
    if (chat_handle_valid(target)) {
        return INDIGO_CHAT_TARGET_HANDLE;
    }
    return INDIGO_CHAT_TARGET_INVALID;
}

bool
indigo_chat_is_self(const char *viewer_did, const char *target_did)
{
    return viewer_did && target_did && viewer_did[0] != '\0' &&
           strcmp(viewer_did, target_did) == 0;
}

bool
indigo_chat_convo_id_copy(char *out, size_t cap, const char *id)
{
    size_t len;

    if (!out || cap == 0) {
        return false;
    }
    out[0] = '\0';
    if (!id || id[0] == '\0') {
        return false;
    }
    len = strlen(id);
    if (len >= cap || len >= INDIGO_CHAT_ID_MAX) {
        return false;
    }
    for (const char *p = id; *p; p++) {
        /* Anything at or below a space, and anything past '~', is refused. A
         * negative char (a high byte on a signed char) falls under the first. */
        if (*p <= ' ' || *p > '~') {
            return false;
        }
    }
    memcpy(out, id, len + 1);
    return true;
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

        /* An id too long to open by is left out rather than shown with a cut id. */
        if (!src->id || strlen(src->id) >= INDIGO_CHAT_ID_MAX) {
            continue;
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

/* --- a group's pending join requests --- */

void
indigo_chat_join_requests_reset(indigo_chat_join_requests *list)
{
    if (!list) {
        return;
    }
    list->convo_id[0] = '\0';
    list->count = 0;
    list->cursor[0] = '\0';
    list->has_more = false;
}

void
indigo_chat_join_requests_clear(indigo_chat_join_requests *list)
{
    if (!list) {
        return;
    }
    indigo_chat_join_requests_reset(list);
    list->selected = 0;
    list->scroll = 0;
    list->loading = false;
    list->status[0] = '\0';
    list->status_is_error = false;
}

bool
indigo_chat_join_requests_can_page(const indigo_chat_join_requests *list)
{
    return list && list->has_more && list->count < INDIGO_CHAT_JOIN_REQUESTS_MAX;
}

const indigo_chat_join_request *
indigo_chat_join_requests_find(const indigo_chat_join_requests *list, const char *did)
{
    if (!list || !did) {
        return NULL;
    }
    for (unsigned i = 0; i < list->count && i < INDIGO_CHAT_JOIN_REQUESTS_MAX; i++) {
        if (strcmp(list->items[i].did, did) == 0) {
            return &list->items[i];
        }
    }
    return NULL;
}

bool
indigo_chat_join_requests_remove(indigo_chat_join_requests *list, const char *did)
{
    if (!list || !did) {
        return false;
    }
    for (unsigned i = 0; i < list->count && i < INDIGO_CHAT_JOIN_REQUESTS_MAX; i++) {
        if (strcmp(list->items[i].did, did) != 0) {
            continue;
        }
        memmove(&list->items[i], &list->items[i + 1],
                (size_t) (list->count - i - 1) * sizeof(list->items[0]));
        list->count--;
        if (list->selected >= list->count) {
            list->selected = list->count > 0 ? list->count - 1 : 0;
        }
        if (list->scroll > list->selected) {
            list->scroll = list->selected;
        }
        return true;
    }
    return false;
}

bool
indigo_chat_join_requests_select(indigo_chat_join_requests *list, unsigned index, unsigned rows)
{
    if (!list || index >= list->count || index == list->selected) {
        return false;
    }
    list->selected = index;
    keep_visible(&list->scroll, index, rows);
    return true;
}

bool
indigo_chat_join_requests_move(indigo_chat_join_requests *list, int delta, unsigned rows)
{
    long target;

    if (!list || list->count == 0) {
        return false;
    }
    target = (long) list->selected + delta;
    if (target < 0) {
        target = 0;
    }
    if (target >= (long) list->count) {
        target = (long) list->count - 1;
    }
    return indigo_chat_join_requests_select(list, (unsigned) target, rows);
}

void
indigo_chat_thread_clear(indigo_chat_thread *thread)
{
    if (!thread) {
        return;
    }
    thread->count = 0;
    thread->selected = 0;
    thread->scroll = 0;
    thread->loading = false;
    thread->status[0] = '\0';
    thread->status_is_error = false;
}

bool
indigo_chat_thread_select(indigo_chat_thread *thread, unsigned index, unsigned rows)
{
    if (!thread || index >= thread->count || index == thread->selected) {
        return false;
    }
    thread->selected = index;
    keep_visible(&thread->scroll, index, rows);
    return true;
}

bool
indigo_chat_thread_move(indigo_chat_thread *thread, int delta, unsigned rows)
{
    long target;

    if (!thread || thread->count == 0) {
        return false;
    }
    target = (long) thread->selected + delta;
    if (target < 0) {
        target = 0;
    }
    if (target >= (long) thread->count) {
        target = (long) thread->count - 1;
    }
    return indigo_chat_thread_select(thread, (unsigned) target, rows);
}

int
indigo_chat_thread_fill_from_wolfram(indigo_chat_thread *out,
                                     const struct wf_chat_message_list *list,
                                     const char *viewer_did)
{
    const wf_chat_message_list *typed = (const wf_chat_message_list *) list;
    size_t n;

    if (!out || !typed) {
        return 0;
    }

    n = typed->message_count < INDIGO_CHAT_MESSAGES_MAX ? typed->message_count
                                                       : INDIGO_CHAT_MESSAGES_MAX;
    indigo_chat_thread_clear(out);
    for (size_t k = 0; k < n; k++) {
        const wf_chat_message *src = &typed->messages[k];
        indigo_chat_message *dst = &out->items[n - 1 - k];

        memset(dst, 0, sizeof *dst);
        indigo_copy_utf8(dst->text, sizeof dst->text, src->text ? src->text : "");
        dst->mine = viewer_did && src->sender && strcmp(src->sender, viewer_did) == 0;
    }
    out->count = (unsigned) n;
    if (out->count > 0) {
        indigo_chat_thread_select(out, out->count - 1, INDIGO_CHAT_THREAD_ROWS);
    }
    return (int) n;
}

int
indigo_chat_join_requests_append_from_wolfram(indigo_chat_join_requests *out,
                                              const struct wf_chat_join_requests *list)
{
    const wf_chat_join_requests *typed = (const wf_chat_join_requests *) list;
    int added = 0;

    if (!out || !typed) {
        return 0;
    }

    for (size_t i = 0; i < typed->item_count; i++) {
        const wf_chat_join_request *src = &typed->items[i];
        const wf_agent_profile_view *who = &src->requested_by;
        const char *did;
        const char *handle;
        const char *display;
        indigo_chat_join_request *item;

        if (out->count >= INDIGO_CHAT_JOIN_REQUESTS_MAX) {
            break;
        }

        /* The DID is the handle approve and reject take, so a request without
         * one cannot be acted on and is not worth a row. */
        did = who->did;
        if (!did || indigo_chat_target_kind_of(did) != INDIGO_CHAT_TARGET_DID) {
            continue;
        }
        if (indigo_chat_join_requests_find(out, did)) {
            continue;
        }

        item = &out->items[out->count];
        memset(item, 0, sizeof *item);
        snprintf(item->did, sizeof item->did, "%s", did);

        handle = who->handle ? who->handle : "";
        display = who->display_name ? who->display_name : "";
        if (display[0] == '\0') {
            display = handle;
        }
        indigo_copy_utf8(item->name, sizeof item->name, display);
        snprintf(item->handle, sizeof item->handle, "@%s", handle);

        out->count++;
        added++;
    }

    /* A full list clears the cursor, as the other lists do, so a screen that
     * auto-pages stops at the cap rather than asking forever. */
    if (out->count >= INDIGO_CHAT_JOIN_REQUESTS_MAX || !typed->cursor || !typed->cursor[0]) {
        out->cursor[0] = '\0';
        out->has_more = false;
    } else {
        indigo_copy_utf8(out->cursor, sizeof out->cursor, typed->cursor);
        out->has_more = true;
    }
    return added;
}

bool
indigo_chat_reply_valid(const char *text)
{
    if (!text || text[0] == '\0' || strlen(text) >= INDIGO_CHAT_REPLY_MAX) {
        return false;
    }
    for (const char *p = text; *p; p++) {
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
            return true;
        }
    }
    return false;
}
