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

/* How many messages a conversation holds, the newest first from the server. */
#define INDIGO_CHAT_MESSAGES_MAX 20
/* A reply in bytes, with room for the terminator. */
#define INDIGO_CHAT_REPLY_MAX    512
/* Rows of the message list on the bottom screen. */
#define INDIGO_CHAT_THREAD_ROWS  3

typedef struct {
   char text[INDIGO_POST_TEXT_MAX];
   bool mine;                             /* sent by the signed-in account */
} indigo_chat_message;

/*
 * One conversation, oldest message first, with the list state the screen keeps.
 * The title and convo id belong to the screen and survive a reload; the rows and
 * selection are replaced by each page.
 */
typedef struct {
   char convo_id[INDIGO_CHAT_ID_MAX];
   char title[INDIGO_POST_NAME_MAX];
   indigo_chat_message items[INDIGO_CHAT_MESSAGES_MAX];
   unsigned count;
   unsigned selected;
   unsigned scroll;
   bool loading;
   char status[INDIGO_POST_NOTE_MAX];
   bool status_is_error;
   /* The reply being written. It survives a failed send and is cleared once sent. */
   char reply[INDIGO_CHAT_REPLY_MAX];
   bool sending;
} indigo_chat_thread;

/* A reply the chat service will take: some text that is not only spaces. */
bool indigo_chat_reply_valid(const char *text);

struct wf_chat_message_list;

/* Clear the rows and list state; the title and convo id are kept. */
void indigo_chat_thread_clear(indigo_chat_thread *thread);
bool indigo_chat_thread_select(indigo_chat_thread *thread, unsigned index, unsigned rows);
bool indigo_chat_thread_move(indigo_chat_thread *thread, int delta, unsigned rows);

/* Replace the rows with a getMessages page. The server sends newest first, so the
 * page is reversed into place and the newest message is selected. */
int indigo_chat_thread_fill_from_wolfram(indigo_chat_thread *out,
                                         const struct wf_chat_message_list *list,
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
