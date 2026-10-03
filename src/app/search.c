#include "app/search.h"

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
indigo_search_clear(indigo_search *s)
{
    s->count = 0;
    s->selected = 0;
    s->scroll = 0;
    s->loading = false;
    s->searched = false;
    s->status[0] = '\0';
    s->status_is_error = false;
}

bool
indigo_search_select(indigo_search *s, unsigned index, unsigned rows)
{
    if (index >= s->count || index == s->selected) {
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

    if (s->count == 0) {
        return false;
    }
    if (target < 0) {
        target = 0;
    }
    if (target >= (long) s->count) {
        target = (long) s->count - 1;
    }
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
        return "Posts";
    case INDIGO_SEARCH_LISTS:
        return "Lists";
    case INDIGO_SEARCH_LIST_MEMBERS:
        return "Members";
    case INDIGO_SEARCH_FEEDS:
        return "Feeds";
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
