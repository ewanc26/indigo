#ifndef INDIGO_STORE_QUEUE_STORE_H
#define INDIGO_STORE_QUEUE_STORE_H

#include "app/post_queue.h"
#include "store/store_status.h"

/* Write the queue to `path`, atomically. */
indigo_store_status indigo_queue_store_save(const char *path, const indigo_post_queue *q);

/* Load the queue from `path`. A missing file is an empty queue, reported as
 * INDIGO_STORE_MISSING. A damaged file is set aside as `path.bad`, the queue
 * is empty, and INDIGO_STORE_UNREADABLE is returned: nothing is lost, and the
 * app still starts. */
indigo_store_status indigo_queue_store_load(const char *path, indigo_post_queue *out);

#endif
