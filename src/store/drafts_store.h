#ifndef INDIGO_STORE_DRAFTS_STORE_H
#define INDIGO_STORE_DRAFTS_STORE_H

#include "app/social.h"
#include "store/store_status.h"

#include <stdbool.h>
#include <stddef.h>

/* Saved composer drafts, separate from the send queue. Nothing in this file
 * can publish: a draft is text plus the reply/quote context needed to put it
 * back in the composer. Images and multi-post threads are not stored yet, so
 * saving one is refused rather than silently dropping part of it.
 *
 * File format v1: a version line, one tab-separated record per draft, then an
 * "end" line. Every string is escaped, and the end marker makes a truncated
 * write fail instead of loading half a draft. */

#define INDIGO_SAVED_DRAFTS_MAX 8

typedef struct {
    char text[INDIGO_DRAFT_MAX];
    long long saved_at;
    indigo_compose_mode mode;
    indigo_reply_gate reply_gate;
    bool has_target;
    char target_uri[INDIGO_POST_URI_MAX];
    char target_cid[INDIGO_POST_CID_MAX];
    char target_handle[INDIGO_POST_NAME_MAX];
    char target_text[INDIGO_POST_TEXT_MAX];
    char root_uri[INDIGO_POST_URI_MAX];
    char root_cid[INDIGO_POST_CID_MAX];
} indigo_saved_draft;

typedef struct {
    indigo_saved_draft items[INDIGO_SAVED_DRAFTS_MAX];
    unsigned count;
} indigo_saved_drafts;

void indigo_saved_drafts_init(indigo_saved_drafts *drafts);

bool indigo_saved_draft_valid(const indigo_saved_draft *draft);

/* Append; false, with nothing stored, when the list is full or the draft is
 * invalid. */
bool indigo_saved_drafts_add(indigo_saved_drafts *drafts,
                             const indigo_saved_draft *draft);

const indigo_saved_draft *indigo_saved_drafts_get(const indigo_saved_drafts *drafts,
                                                  unsigned index);

/* Remove one; the rest keep their order. */
bool indigo_saved_drafts_remove(indigo_saved_drafts *drafts, unsigned index);

indigo_codec_status indigo_drafts_encode(const indigo_saved_drafts *drafts,
                                         char *out, size_t cap, size_t *len);
indigo_codec_status indigo_drafts_decode(const char *data, size_t len,
                                         indigo_saved_drafts *out);

indigo_store_status indigo_drafts_store_save(const char *path,
                                             const indigo_saved_drafts *drafts);
/* A missing file is an empty list. A damaged one is set aside and reported
 * unreadable; nothing is deleted. */
indigo_store_status indigo_drafts_store_load(const char *path,
                                             indigo_saved_drafts *out);

#endif
