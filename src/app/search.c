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
    return s->selected < s->count ? &s->items[s->selected] : NULL;
}

const indigo_actor *
indigo_search_row(const indigo_search *s, unsigned row, unsigned rows)
{
    unsigned index;

    if (rows == 0 || row >= rows) {
        return NULL;
    }
    index = s->scroll + row;
    return index < s->count ? &s->items[index] : NULL;
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
    case INDIGO_SEARCH_PEOPLE:
        break;
    }
    return "Search";
}

/* Only the search mode has something to type; the other two are a list around
 * one person, and offering a query box there would invite a search that
 * nothing acts on. */
bool
indigo_search_is_typed(const indigo_search *s)
{
    return s->kind == INDIGO_SEARCH_PEOPLE;
}
