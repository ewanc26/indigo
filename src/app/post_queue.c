#include "app/post_queue.h"

#include <string.h>

void
indigo_queue_init(indigo_post_queue *q)
{
    memset(q, 0, sizeof *q);
}

bool
indigo_queue_add(indigo_post_queue *q, const char *text, long long now)
{
    size_t len;

    if (!q || !text || q->count >= INDIGO_QUEUE_MAX) {
        return false;
    }
    len = strlen(text);
    if (len == 0 || len >= INDIGO_QUEUE_TEXT_MAX) {
        return false;
    }

    indigo_queued_post *p = &q->items[q->count];
    memcpy(p->text, text, len + 1);
    p->queued_at = now;
    q->count++;
    return true;
}

const indigo_queued_post *
indigo_queue_get(const indigo_post_queue *q, unsigned index)
{
    if (!q || index >= q->count) {
        return NULL;
    }
    return &q->items[index];
}

/* Close the gap at `index`, keeping the order of the rest. */
static void
remove_at(indigo_post_queue *q, unsigned index)
{
    for (unsigned i = index + 1; i < q->count; i++) {
        q->items[i - 1] = q->items[i];
    }
    q->count--;
    memset(&q->items[q->count], 0, sizeof q->items[q->count]);
}

bool
indigo_queue_discard(indigo_post_queue *q, unsigned index)
{
    if (!q || index >= q->count) {
        return false;
    }
    remove_at(q, index);
    return true;
}

bool
indigo_queue_take(indigo_post_queue *q, unsigned index, indigo_queued_post *out)
{
    if (!q || !out || index >= q->count) {
        return false;
    }
    *out = q->items[index];
    remove_at(q, index);
    return true;
}
