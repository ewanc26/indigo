#include "app/social.h"

#include <stdio.h>
#include <string.h>

static void
keep_visible(unsigned *scroll, unsigned selected, unsigned rows)
{
    if (selected < *scroll) {
        *scroll = selected;
    } else if (selected >= *scroll + rows) {
        *scroll = selected + 1 - rows;
    }
}

void
indigo_notifications_clear(indigo_notifications *n)
{
    n->count = 0;
    n->selected = 0;
    n->scroll = 0;
    n->loading = false;
    n->status[0] = '\0';
    n->status_is_error = false;
}

bool
indigo_notifications_select(indigo_notifications *n, unsigned index, unsigned rows)
{
    if (index >= n->count || index == n->selected) {
        return false;
    }
    n->selected = index;
    keep_visible(&n->scroll, n->selected, rows);
    return true;
}

bool
indigo_notifications_move(indigo_notifications *n, int delta, unsigned rows)
{
    long target = (long) n->selected + delta;

    if (n->count == 0) {
        return false;
    }
    if (target < 0) {
        target = 0;
    }
    if (target >= (long) n->count) {
        target = (long) n->count - 1;
    }
    return indigo_notifications_select(n, (unsigned) target, rows);
}

const indigo_notification *
indigo_notifications_selected(const indigo_notifications *n)
{
    return n->selected < n->count ? &n->items[n->selected] : NULL;
}

bool
indigo_compose_ready(const indigo_compose *c)
{
    return !c->sending && c->text[0] != '\0';
}

bool
indigo_compose_can_toggle(const indigo_compose *c)
{
    return c->has_target && !c->sending;
}

void
indigo_compose_toggle(indigo_compose *c)
{
    if (!indigo_compose_can_toggle(c)) {
        return;
    }
    c->mode = c->mode == INDIGO_COMPOSE_REPLY ? INDIGO_COMPOSE_QUOTE : INDIGO_COMPOSE_REPLY;
}

bool
indigo_compose_can_gate(const indigo_compose *c)
{
    /* A top-level post is the only mode with no target, and it is the only one
     * a threadgate can usefully hang off. This asks which control the compose
     * screen is showing, not whether it may be moved right now. */
    return c->mode == INDIGO_COMPOSE_POST && !c->has_target;
}

void
indigo_compose_gate_cycle(indigo_compose *c)
{
    /* Frozen mid-send, as the reply/quote switch is, so the pill does not
     * change under the thumb that just hit Post. */
    if (!indigo_compose_can_gate(c) || c->sending) {
        return;
    }
    c->reply_gate =
        (indigo_reply_gate) (((int) c->reply_gate + 1) % (int) INDIGO_REPLY_GATE_COUNT);
}

const char *
indigo_compose_gate_label(const indigo_compose *c)
{
    switch (c->reply_gate) {
    case INDIGO_REPLY_GATE_FOLLOWED_MENTIONED:
        return "People you follow and mention";
    case INDIGO_REPLY_GATE_NOBODY:
        return "Nobody";
    case INDIGO_REPLY_GATE_EVERYONE:
    default:
        return "Everyone";
    }
}

const char *
indigo_compose_gate_short(const indigo_compose *c)
{
    switch (c->reply_gate) {
    case INDIGO_REPLY_GATE_FOLLOWED_MENTIONED:
        return "Follows + mentions";
    case INDIGO_REPLY_GATE_NOBODY:
        return "Nobody";
    case INDIGO_REPLY_GATE_EVERYONE:
    default:
        return "Everyone";
    }
}

static void
add_item(indigo_menu *m, indigo_menu_kind kind, const char *label, const char *payload)
{
    indigo_menu_item *it;

    if (m->count >= INDIGO_MENU_MAX) {
        return;
    }
    it = &m->items[m->count++];
    it->kind = kind;
    indigo_copy_utf8(it->label, sizeof it->label, label);
    indigo_copy_utf8(it->payload, sizeof it->payload, payload ? payload : "");
}

/* A facet is labelled with the text the person can see in the post, which is
 * also what tells them which mention or link they are choosing. */
static void
add_facet(indigo_menu *m, const indigo_post *post, const indigo_post_facet *f)
{
    char raw[INDIGO_POST_TEXT_MAX + 1];
    char text[INDIGO_POST_NAME_MAX * 2];
    char label[INDIGO_POST_NAME_MAX * 2 + 16];
    const char *prefix;
    indigo_menu_kind kind;
    size_t len = strlen(post->text);
    size_t start = f->start < len ? f->start : len;
    size_t end = f->end < len ? f->end : len;

    if (!f->target[0] || end <= start) {
        return;
    }
    memcpy(raw, post->text + start, end - start);
    raw[end - start] = '\0';

    switch (f->kind) {
    case INDIGO_FACET_MENTION:
        kind = INDIGO_MENU_OPEN_MENTION;
        prefix = "Profile: ";
        break;
    case INDIGO_FACET_TAG:
        kind = INDIGO_MENU_SHOW_TAG;
        prefix = "Tag: ";
        break;
    default:
        kind = INDIGO_MENU_SHOW_LINK;
        prefix = "Link: ";
        break;
    }
    /* Truncate on a UTF-8 boundary first so the prefix and the label fit. */
    indigo_copy_utf8(text, sizeof text, raw);
    snprintf(label, sizeof label, "%s%s", prefix, text);
    add_item(m, kind, label, f->target);
}

void
indigo_menu_build(indigo_menu *m, const indigo_post *post, const char *account)
{
    memset(m, 0, sizeof *m);
    if (post) {
        for (unsigned i = 0; i < post->facet_count; i++) {
            add_facet(m, post, &post->facets[i]);
        }
    }
    add_item(m, INDIGO_MENU_COMPOSE, "New post", "");
    add_item(m, INDIGO_MENU_NOTIFICATIONS, "Notifications", "");
    add_item(m, INDIGO_MENU_FIND_PEOPLE, "Find people", "");
    add_item(m, INDIGO_MENU_FIND_POSTS, "Find posts", "");
    add_item(m, INDIGO_MENU_LISTS, "Lists", "");
    add_item(m, INDIGO_MENU_FEEDS, "Feeds", "");
    add_item(m, INDIGO_MENU_MY_PROFILE,
             account && account[0] ? "My profile" : "Your profile", "");
    add_item(m, INDIGO_MENU_SIGN_OUT, "Sign out", "");
    add_item(m, INDIGO_MENU_CLOSE, "Close menu", "");
}

bool
indigo_menu_move(indigo_menu *m, int delta, unsigned rows)
{
    long target = (long) m->selected + delta;

    if (m->count == 0) {
        return false;
    }
    if (target < 0) {
        target = 0;
    }
    if (target >= (long) m->count) {
        target = (long) m->count - 1;
    }
    if ((unsigned) target == m->selected) {
        return false;
    }
    m->selected = (unsigned) target;
    keep_visible(&m->scroll, m->selected, rows);
    return true;
}

const indigo_menu_item *
indigo_menu_row(const indigo_menu *m, unsigned row, unsigned rows)
{
    unsigned index;

    if (rows == 0 || row >= rows) {
        return NULL;
    }
    index = m->scroll + row;
    return index < m->count ? &m->items[index] : NULL;
}
