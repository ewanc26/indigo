#ifndef INDIGO_MEDIA_LOADER_H
#define INDIGO_MEDIA_LOADER_H

#include "media/media.h"

#include <stdbool.h>

/*
 * The network half of the image pipeline: one thread that fetches, decodes and
 * hands finished images back to the main thread.
 *
 * The split is not caution, it is ownership. A decode can take a second or
 * more on a 268MHz ARM11, and uploading a texture writes GPU state, so neither
 * belongs anywhere near the frame loop. The loader only ever calls Wolfram and
 * free(); every texture is created by the backend on the main thread.
 *
 * Nothing here writes the cache. The main thread claims a slot when it asks for
 * a URL and adopts results once per frame with indigo_media_loader_drain(), so
 * a decode that lands mid-frame cannot move a slot out from under the code
 * drawing it.
 */

bool indigo_media_loader_start(void);
void indigo_media_loader_stop(void);

/* Asks for `url` to be fetched, claiming a cache slot for it as it goes. Cheap
 * and safe to call every frame for an image that is not there yet: a URL the
 * cache already knows in any state is dropped. `max_dim` is the longest decoded
 * side the caller will draw it at, or 0 for the default. Returns false when the
 * loader is not running or the queue is full, which is not an error -- the next
 * frame asks again. */
bool indigo_media_loader_request(indigo_media_cache *c, const char *url,
                                 unsigned max_dim);

/* Adopts everything the loader has finished into `c`. Main thread only, once
 * per frame. */
void indigo_media_loader_drain(indigo_media_cache *c);

#endif