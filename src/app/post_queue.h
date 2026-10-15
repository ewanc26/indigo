#ifndef INDIGO_APP_POST_QUEUE_H
#define INDIGO_APP_POST_QUEUE_H

/* The offline compose queue: posts written while the 3DS had no connection,
 * kept until the person sends or discards them (AGENTS.md section 15).
 *
 * Storage only, and that is deliberate. Nothing here touches the network, and
 * nothing publishes on its own. A post leaves the queue only through
 * indigo_queue_take, called for one post at a time on the person's request.
 * The matching controls are: inspect (indigo_queue_get), retry (take, then put
 * back with indigo_queue_add if the send fails) and discard
 * (indigo_queue_discard).
 *
 * Text that would have to be cut is refused, not shortened: a post that is
 * silently shortened is not the post the person wrote. */

#include <stdbool.h>
#include <stddef.h>

#define INDIGO_QUEUE_MAX 8
#define INDIGO_QUEUE_TEXT_MAX 1024

typedef struct {
    char text[INDIGO_QUEUE_TEXT_MAX];
    /* Seconds since the epoch, from the caller's clock. */
    long long queued_at;
} indigo_queued_post;

typedef struct {
    indigo_queued_post items[INDIGO_QUEUE_MAX];
    unsigned count;
} indigo_post_queue;

void indigo_queue_init(indigo_post_queue *q);

/* Append a post. False, with nothing stored, when the queue is full, the text
 * is empty or NULL, or it does not fit INDIGO_QUEUE_TEXT_MAX - 1 bytes. */
bool indigo_queue_add(indigo_post_queue *q, const char *text, long long now);

/* The post at `index` (oldest first), or NULL when out of range. */
const indigo_queued_post *indigo_queue_get(const indigo_post_queue *q, unsigned index);

/* Discard the post at `index`; the rest keep their order. False when out of
 * range. */
bool indigo_queue_discard(indigo_post_queue *q, unsigned index);

/* Remove the post at `index` and copy it to `out`, for the person to send.
 * False when out of range. */
bool indigo_queue_take(indigo_post_queue *q, unsigned index, indigo_queued_post *out);

#endif
