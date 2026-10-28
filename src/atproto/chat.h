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
 * The inbox, the request inbox, a group's pending join requests, starting a
 * conversation, accepting or declining a request, approving or rejecting a join
 * request, reading messages and replying all live here. The shapes follow
 * Cobalt's atproto/chat.h, so the two clients settle on one design (indigo#22).
 */

#include "app/timeline.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INDIGO_CHAT_CONVOS_MAX  30
/* A conversation id as the bridge accepts it: 1 to 128 characters (validConvoId in
 * bridge/src/domain/api.ts), plus the terminator. */
#define INDIGO_CHAT_ID_MAX      129
#define INDIGO_CHAT_PREVIEW_MAX 128
/* A handle or DID typed to start a conversation, with its terminator. A handle
 * is at most 253 bytes on the wire, but the keyboard and the service both take
 * far less; 128 is the same bound Cobalt uses and well inside the job's URI
 * field. */
#define INDIGO_CHAT_TARGET_MAX  128

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

/* True while there is another page and room to hold it. */
bool indigo_chat_inbox_can_page(const indigo_chat_inbox *inbox);

/* The row for `convo_id`, borrowed, or NULL when there is none. */
const indigo_chat_convo *indigo_chat_inbox_find(const indigo_chat_inbox *inbox,
                                                const char *convo_id);

/*
 * Drop the row for `convo_id`, keeping the order of the rest. Its unread count
 * leaves the inbox total. False when no row has that id, or `inbox` is NULL.
 */
bool indigo_chat_inbox_remove(indigo_chat_inbox *inbox, const char *convo_id);

/*
 * Put `item` at the front of `inbox`, dropping the last row when it is full.
 * False when `inbox` or `item` is NULL, or a row with the same id is already
 * held, so one conversation never appears twice.
 */
bool indigo_chat_inbox_add_front(indigo_chat_inbox *inbox, const indigo_chat_convo *item);

/* Select `index` and keep it inside a window of `rows`. False when it is out of
 * range or already selected. */
bool indigo_chat_inbox_select(indigo_chat_inbox *inbox, unsigned index, unsigned rows);

/* Move the selection by `delta`, clamped to the list. */
bool indigo_chat_inbox_move(indigo_chat_inbox *inbox, int delta, unsigned rows);

/* The request inbox's count as text: "No requests", "1 request" or "N requests".
 * False when `out` is NULL, `cap` is 0 or the text does not fit in `cap` bytes
 * with its terminator; `out` is then left empty. */
bool indigo_chat_requests_label(int count, char *out, size_t cap);

/* Index of the first member whose DID is not the viewer's, or -1 when there is
 * none. A convo with only the viewer has nobody to show. */
int indigo_chat_pick_counterpart(const char *const *member_dids, size_t count,
                                 const char *viewer_did);

/* Number of members other than the viewer. Above one is a group. */
int indigo_chat_other_member_count(const char *const *member_dids, size_t count,
                                   const char *viewer_did);

/*
 * Starting a conversation with an account the inbox does not hold yet. The
 * target is a handle or a DID as typed. A handle has to be resolved to a DID
 * first, because getConvoForMembers takes DIDs only.
 */
typedef enum {
   INDIGO_CHAT_TARGET_INVALID = 0,
   INDIGO_CHAT_TARGET_DID,
   INDIGO_CHAT_TARGET_HANDLE,
} indigo_chat_target_kind;

/*
 * Copy `input` into `out` with surrounding spaces, tabs and line breaks, and
 * one leading '@', removed. False when nothing is left, or the result would
 * not fit in `cap` bytes with its terminator.
 */
bool indigo_chat_target_normalise(const char *input, char *out, size_t cap);

/*
 * Whether a normalised target is a DID (did:<method>:<id>) or a handle in the
 * atproto syntax: dot-separated labels, at least two, with a TLD that starts
 * with a letter. Anything else, including a handle with a space or an
 * underscore, is INDIGO_CHAT_TARGET_INVALID.
 */
indigo_chat_target_kind indigo_chat_target_kind_of(const char *target);

/* True when both DIDs are set and name the same account. */
bool indigo_chat_is_self(const char *viewer_did, const char *target_did);

/*
 * Copy a convo id out of a server response. The id has to be non-empty, use
 * only printable ASCII without spaces, and fit both `cap` and
 * INDIGO_CHAT_ID_MAX.
 */
bool indigo_chat_convo_id_copy(char *out, size_t cap, const char *id);

/* --- a group's pending join requests --- */

/*
 * Who asked to join a group, flattened for drawing. Only the group's owner can
 * list these, so a screen opens them from the group's own conversation. The DID
 * is the handle the approve and reject calls take, so a row keeps it whether or
 * not the name could be read.
 */
#define INDIGO_CHAT_JOIN_REQUESTS_MAX 30

typedef struct {
   char did[INDIGO_CHAT_TARGET_MAX];   /* who asked, for the decision */
   char name[INDIGO_POST_NAME_MAX];    /* display name, or the handle */
   char handle[INDIGO_POST_NAME_MAX];  /* with a leading '@' */
} indigo_chat_join_request;

typedef struct {
   char convo_id[INDIGO_CHAT_ID_MAX];  /* the group these belong to */
   indigo_chat_join_request items[INDIGO_CHAT_JOIN_REQUESTS_MAX];
   unsigned count;
   char cursor[INDIGO_CURSOR_MAX];
   bool has_more;
   /* The list state the screen keeps, as indigo_chat_inbox does. */
   unsigned selected;
   unsigned scroll;
   bool loading;
   char status[INDIGO_POST_NOTE_MAX];
   bool status_is_error;
} indigo_chat_join_requests;

void indigo_chat_join_requests_reset(indigo_chat_join_requests *list);

/* Reset the rows and the list state too: selection, loading and status. */
void indigo_chat_join_requests_clear(indigo_chat_join_requests *list);

/* True while there is another page and room to hold it. */
bool indigo_chat_join_requests_can_page(const indigo_chat_join_requests *list);

/* The row for `did`, borrowed, or NULL when there is none. */
const indigo_chat_join_request *indigo_chat_join_requests_find(
   const indigo_chat_join_requests *list, const char *did);

/* Drop the request from `did`, keeping the order of the rest. False when no row
 * has that DID, or `list` is NULL. Called once a decision has been accepted, so
 * the row leaves without a refetch. */
bool indigo_chat_join_requests_remove(indigo_chat_join_requests *list, const char *did);

bool indigo_chat_join_requests_select(indigo_chat_join_requests *list, unsigned index,
                                      unsigned rows);
bool indigo_chat_join_requests_move(indigo_chat_join_requests *list, int delta, unsigned rows);

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

struct wf_chat_join_requests;

/*
 * Append every request in a parsed listJoinRequests page. Returns the number
 * added. A request whose DID is already held is skipped, so paging never
 * doubles a row; a full list clears the cursor, as the other lists do.
 */
int indigo_chat_join_requests_append_from_wolfram(indigo_chat_join_requests *out,
                                                  const struct wf_chat_join_requests *list);

#ifdef __cplusplus
}
#endif
