#include "app/search.h"

#include <string.h>
#include <wolfram/profile_tab.h>

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
indigo_search_clear(indigo_search *s)
{
    s->count = 0;
    s->selected = 0;
    s->scroll = 0;
    s->loading = false;
    s->searched = false;
    s->list_editing = false;
    s->list_remove_confirm = -1;
    s->cursor[0] = '\0';
    s->has_more = false;
    s->status[0] = '\0';
    s->status_is_error = false;
}

unsigned
indigo_search_row_count(const indigo_search *s)
{
    if (!s) {
        return 0;
    }
    if (s->kind == INDIGO_SEARCH_STARTER_PACK) {
        return s->count + s->pack.feed_count;
    }
    return s->count;
}

const char *
indigo_search_selected_feed(const indigo_search *s)
{
    unsigned index;

    if (!s || s->kind != INDIGO_SEARCH_STARTER_PACK || s->selected < s->count) {
        return NULL;
    }
    index = s->selected - s->count;
    return index < s->pack.feed_count ? s->pack.feeds[index] : NULL;
}

const char *
indigo_search_row_feed(const indigo_search *s, unsigned row, unsigned rows)
{
    unsigned index;

    if (!s || s->kind != INDIGO_SEARCH_STARTER_PACK || row >= rows) {
        return NULL;
    }
    index = s->scroll + row;
    if (index < s->count) {
        return NULL;
    }
    index -= s->count;
    return index < s->pack.feed_count ? s->pack.feeds[index] : NULL;
}

bool
indigo_search_wants_page(const indigo_search *s)
{
    if (s->loading || !s->has_more || s->cursor[0] == '\0') {
        return false;
    }
    if (s->kind == INDIGO_SEARCH_STARTER_PACK && s->selected >= s->count) {
        return false;
    }
    /* Full is full: another page would append nothing while holding a cursor
     * that reads as "more available" on a list that cannot grow. */
    if (s->count >= INDIGO_SEARCH_MAX) {
        return false;
    }
    return s->selected + INDIGO_SEARCH_ROWS + 1 >= s->count;
}

void
indigo_search_begin_page(indigo_search *s, bool append)
{
    if (!append) {
        indigo_search_clear(s);
    }
    s->loading = true;
    s->status[0] = '\0';
    s->status_is_error = false;
}

void
indigo_search_finish_page(indigo_search *s, unsigned added,
                          const char *next_cursor)
{
    s->loading = false;
    s->searched = true;
    /* A page that appended nothing is the end, even when the server still
     * offers a cursor: paging further would append nothing forever. */
    const bool empty_page = added == 0 && s->count > 0;

    if (empty_page) {
        s->cursor[0] = '\0';
    } else {
        indigo_copy_utf8(s->cursor, sizeof s->cursor, next_cursor ? next_cursor : "");
    }
    s->has_more = s->cursor[0] != '\0';
}

void
indigo_search_fail_page(indigo_search *s, const char *message)
{
    s->loading = false;
    indigo_copy_utf8(s->status, sizeof s->status, message ? message : "");
    s->status_is_error = true;
}

bool
indigo_search_select(indigo_search *s, unsigned index, unsigned rows)
{
    if (index >= indigo_search_row_count(s) || index == s->selected) {
        return false;
    }
    s->selected = index;
    keep_visible(&s->scroll, s->selected, rows);
    return true;
}

bool
indigo_search_move(indigo_search *s, int delta, unsigned rows)
{
    long target = (long) s->selected + delta;
    const unsigned total = indigo_search_row_count(s);

    if (total == 0) {
        return false;
    }
    if (target < 0) {
        target = 0;
    }
    if (target >= (long) total) {
        target = (long) total - 1;
    }
    return indigo_search_select(s, (unsigned) target, rows);
    return indigo_search_select(s, (unsigned) target, rows);
}

const indigo_actor *
indigo_search_selected(const indigo_search *s)
{
    return s->selected < s->count ? &s->results.actors[s->selected] : NULL;
}

const indigo_post *
indigo_search_selected_post(const indigo_search *s)
{
    return s->selected < s->count ? &s->results.posts[s->selected] : NULL;
}

const indigo_actor *
indigo_search_row(const indigo_search *s, unsigned row, unsigned rows)
{
    unsigned index;

    if (rows == 0 || row >= rows) {
        return NULL;
    }
    index = s->scroll + row;
    return index < s->count ? &s->results.actors[index] : NULL;
}

const indigo_post *
indigo_search_row_post(const indigo_search *s, unsigned row, unsigned rows)
{
    unsigned index;

    if (rows == 0 || row >= rows) {
        return NULL;
    }
    index = s->scroll + row;
    return index < s->count ? &s->results.posts[index] : NULL;
}

bool
indigo_search_can_submit(const indigo_search *s)
{
    return !s->loading && s->query[0] != '\0';
}

const char *
indigo_search_title(const indigo_search *s)
{
    switch (s->kind) {
    case INDIGO_SEARCH_FOLLOWERS:
        return "Followers";
    case INDIGO_SEARCH_FOLLOWING:
        return "Following";
    case INDIGO_SEARCH_POSTS:
        return "Post search";
    case INDIGO_SEARCH_AUTHOR:
      return wf_profile_tab_name(s->tab);
    case INDIGO_SEARCH_LISTS:
        return "Lists";
    case INDIGO_SEARCH_LIST_MEMBERS:
        return "Members";
    case INDIGO_SEARCH_FEEDS:
        return "Feeds";
    case INDIGO_SEARCH_MUTED:
        return "Muted";
    case INDIGO_SEARCH_BLOCKED:
        return "Blocked";
    case INDIGO_SEARCH_LIKED_BY:
        return "Liked by";
    case INDIGO_SEARCH_REPOSTED_BY:
        return "Reposted by";
    case INDIGO_SEARCH_STARTER_PACK:
        return s->pack.name[0] ? s->pack.name : "Starter pack";
    case INDIGO_SEARCH_PEOPLE:
        break;
    }
    return "Search";
}

/* Only the two search modes have something to type. The people lists are a
 * list around one person, and offering a query box there would invite a
 * search that nothing acts on. */
bool
indigo_search_is_typed(const indigo_search *s)
{
    return s->kind == INDIGO_SEARCH_PEOPLE || s->kind == INDIGO_SEARCH_POSTS;
}

bool
indigo_search_is_posts(const indigo_search *s)
{
    return s->kind == INDIGO_SEARCH_POSTS || s->kind == INDIGO_SEARCH_AUTHOR;
}

static bool
is_list_rows(const indigo_search *s)
{
    return s->kind == INDIGO_SEARCH_LISTS || s->kind == INDIGO_SEARCH_LIST_MEMBERS
        || s->kind == INDIGO_SEARCH_FEEDS;
}

const char *
indigo_list_purpose_label(const char *purpose)
{
    return purpose && strcmp(purpose, INDIGO_LIST_PURPOSE_MODERATE) == 0
               ? "Moderation" : "Curation";
}

const char *
indigo_list_purpose_next(const char *purpose)
{
    return purpose && strcmp(purpose, INDIGO_LIST_PURPOSE_MODERATE) == 0
               ? INDIGO_LIST_PURPOSE_CURATE : INDIGO_LIST_PURPOSE_MODERATE;
}

bool
indigo_search_list_edit_valid(const char *name, const char *description,
                               const char *purpose)
{
    if (!name || !description || !purpose || !name[0]) {
        return false;
    }
    if (strlen(name) >= INDIGO_POST_NAME_MAX ||
        strlen(description) >= INDIGO_POST_NOTE_MAX) {
        return false;
    }
    return strcmp(purpose, INDIGO_LIST_PURPOSE_CURATE) == 0 ||
           strcmp(purpose, INDIGO_LIST_PURPOSE_MODERATE) == 0;
}

bool
indigo_search_is_lists(const indigo_search *s)
{
    return is_list_rows(s);
}

const indigo_list *
indigo_search_selected_list(const indigo_search *s)
{
    if (!indigo_search_is_lists(s) || s->selected >= s->count) {
        return NULL;
    }
    return &s->results.lists[s->selected];
}

const indigo_list *
indigo_search_row_list(const indigo_search *s, unsigned row, unsigned rows)
{
    unsigned index = s->scroll + row;

    if (!indigo_search_is_lists(s) || row >= rows || index >= s->count) {
        return NULL;
    }
    return &s->results.lists[index];
}
