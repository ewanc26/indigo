#ifndef INDIGO_STARTER_PACK_H
#define INDIGO_STARTER_PACK_H

#include "app/timeline.h"

#include <stdbool.h>
#include <stddef.h>

/* A starter pack (app.bsky.graph.starterpack). The pack itself names the
 * creator, description and included feeds; its suggested accounts are the
 * members of the underlying list, fetched separately into the search screen's
 * bounded actor array. Read/use only, like the rest of this pass: creating a
 * pack needs a list editor and a record writer that are not part of browse. */
#define INDIGO_STARTER_PACK_FEEDS_MAX 8

typedef struct {
    char uri[INDIGO_POST_URI_MAX];
    char name[INDIGO_POST_NAME_MAX];
    char creator_handle[INDIGO_POST_NAME_MAX];
    char creator_name[INDIGO_POST_NAME_MAX];
    char creator_avatar[INDIGO_MEDIA_URL_MAX];
    char description[INDIGO_POST_NOTE_MAX];
    char list_uri[INDIGO_POST_URI_MAX];
    unsigned member_count;
    char feeds[INDIGO_STARTER_PACK_FEEDS_MAX][INDIGO_POST_URI_MAX];
    unsigned feed_count;
} indigo_starter_pack;

void indigo_starter_pack_reset(indigo_starter_pack *pack);

/* Accepts an app.bsky.graph.starterpack AT-URI or a bsky.app starter-pack
 * link and writes the canonical AT-URI to `out`. False for anything else or
 * when the result does not fit. */
bool indigo_starter_pack_uri_from_link(const char *link, char *out, size_t out_size);

/* Fill from Wolfram's owned starter-pack view, including the record's list URI
 * and feed references. Returns the number of feeds copied (at most
 * INDIGO_STARTER_PACK_FEEDS_MAX). */
struct wf_graph_starter_pack_view;
int indigo_starter_pack_fill_from_wolfram(
    indigo_starter_pack *pack, const struct wf_graph_starter_pack_view *src);

#endif
