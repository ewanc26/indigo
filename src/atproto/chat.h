#pragma once

/*
 * Direct message conversations, flattened for the 3DS: the read-only inbox.
 *
 * The same shape Cobalt uses (cobalt's atproto/chat.h), so the two clients
 * settle on one design before the screen is built (indigo#22). Wolfram keeps
 * each convo as an owned subtree with the members as profile views; a convo
 * does not say who the viewer is, so the counterpart is the member whose DID
 * is not the signed-in account's.
 *
 * Only the inbox lives here. Reading messages, replying and requests come
 * later.
 */

#include "app/timeline.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INDIGO_CHAT_CONVOS_MAX  30
#define INDIGO_CHAT_ID_MAX      64
#define INDIGO_CHAT_PREVIEW_MAX 128

typedef struct {
   char id[INDIGO_CHAT_ID_MAX];           /* convo id, for opening it later */
   char name[INDIGO_POST_NAME_MAX];       /* counterpart, or "Group" */
   char handle[INDIGO_POST_NAME_MAX];     /* with a leading @, empty for a group */
   char preview[INDIGO_CHAT_PREVIEW_MAX]; /* last message text, empty if none */
   int unread;                            /* unread messages in this convo */
   bool group;                            /* more than one other member */
} indigo_chat_convo;

typedef struct {
   indigo_chat_convo items[INDIGO_CHAT_CONVOS_MAX];
   unsigned count;
   int unread;                            /* unread messages across the inbox */
   char cursor[INDIGO_CURSOR_MAX];
   bool has_more;
   /* The list state the screen keeps, as indigo_notifications does. */
   unsigned selected;
   unsigned scroll;
   bool loading;
   char status[INDIGO_POST_NOTE_MAX];
   bool status_is_error;
} indigo_chat_inbox;

void indigo_chat_inbox_reset(indigo_chat_inbox *inbox);

/* Reset the rows and the list state too: selection, loading and status. */
void indigo_chat_inbox_clear(indigo_chat_inbox *inbox);

/* Select `index` and keep it inside a window of `rows`. False when it is out of
 * range or already selected. */
bool indigo_chat_inbox_select(indigo_chat_inbox *inbox, unsigned index, unsigned rows);

/* Move the selection by `delta`, clamped to the list. */
bool indigo_chat_inbox_move(indigo_chat_inbox *inbox, int delta, unsigned rows);

/* Index of the first member whose DID is not the viewer's, or -1 when there is
 * none. A convo with only the viewer has nobody to show. */
int indigo_chat_pick_counterpart(const char *const *member_dids, size_t count,
                                 const char *viewer_did);

/* Number of members other than the viewer. Above one is a group. */
int indigo_chat_other_member_count(const char *const *member_dids, size_t count,
                                   const char *viewer_did);

struct wf_chat_convo_list;

/* Append every convo in a parsed listConvos page that has a counterpart.
 * Returns the number added. Reaching the cap marks the inbox as having no
 * more, so a screen that auto-pages stops there. */
int indigo_chat_inbox_append_from_wolfram(indigo_chat_inbox *out,
                                          const struct wf_chat_convo_list *list,
                                          const char *viewer_did);

#ifdef __cplusplus
}
#endif
