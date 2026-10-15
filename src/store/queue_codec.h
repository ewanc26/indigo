#ifndef INDIGO_STORE_QUEUE_CODEC_H
#define INDIGO_STORE_QUEUE_CODEC_H

/* The offline compose queue on the SD card, so a saved post survives closing
 * the app (AGENTS.md section 15).
 *
 * File format v1: a version line, one line per post, then an "end" line. Each
 * post line is "<queued_at><TAB><text>", with the text escaped (backslash,
 * newline, carriage return, tab) so one post is always one line. A missing or
 * damaged file never blocks boot: the store sets it aside and the queue starts
 * empty. */

#include "app/post_queue.h"
#include "store/store_status.h"

#include <stddef.h>

#define INDIGO_QUEUE_FORMAT_VERSION 1

/* Encode `q`. INDIGO_CODEC_TOO_BIG when it does not fit `cap`. */
indigo_codec_status indigo_queue_encode(const indigo_post_queue *q, char *out,
                                        size_t cap, size_t *len);

/* Decode `data`. Any failure leaves `out` empty, so a damaged queue is never
 * half-loaded. */
indigo_codec_status indigo_queue_decode(const char *data, size_t len,
                                        indigo_post_queue *out);

#endif
