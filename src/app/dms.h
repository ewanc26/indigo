#ifndef INDIGO_DMS_H
#define INDIGO_DMS_H

#include "app/timeline.h"

#include <stdbool.h>

#define INDIGO_CONVO_MAX 30
#define INDIGO_MESSAGE_MAX 80
#define INDIGO_DM_NAME_MAX INDIGO_POST_NAME_MAX
#define INDIGO_DM_TEXT_MAX INDIGO_POST_TEXT_MAX

typedef enum {
    INDIGO_CONVO_OTHER = 0,
    INDIGO_CONVO_SELF,
} indigo_convo_kind;

typedef struct {
    char id[INDIGO_POST_URI_MAX];
    indigo_convo_kind kind;
    char handle[INDIGO_DM_NAME_MAX];
    char name[INDIGO_DM_NAME_MAX];
    char avatar[INDIGO_MEDIA_URL_MAX];
    char last_text[INDIGO_DM_TEXT_MAX];
    unsigned unread;
    bool has_unread;
    bool loading;
    char status[INDIGO_POST_NOTE_MAX];
    bool status_is_error;
} indigo_convo;

typedef struct {
    char id[INDIGO_POST_URI_MAX];
    char sender[INDIGO_DM_NAME_MAX];
    char avatar[INDIGO_MEDIA_URL_MAX];
    char text[INDIGO_DM_TEXT_MAX];
    char sent_at[INDIGO_POST_NOTE_MAX];
    bool is_mine;
} indigo_message;

typedef struct {
    indigo_convo convos[INDIGO_CONVO_MAX];
    unsigned count;
    unsigned selected;
    unsigned scroll;
    char cursor[INDIGO_CURSOR_MAX];
    bool has_more;
    bool loading;
    char status[INDIGO_POST_NOTE_MAX];
    bool status_is_error;
    /* When non-empty, this screen shows the messages of `active`; the convo
     * list is hidden. Empty shows the convo list. */
    char active_id[INDIGO_POST_URI_MAX];
    indigo_message messages[INDIGO_MESSAGE_MAX];
    unsigned message_count;
    unsigned scroll_msg;
    bool loading_messages;
    char status_msg[INDIGO_POST_NOTE_MAX];
    bool status_msg_is_error;
    /* Compose field for a new message in the active convo. */
    char draft[INDIGO_DM_TEXT_MAX];
    bool sending;
} indigo_dms;

void indigo_dms_clear_list(indigo_dms *d);
void indigo_dms_clear_messages(indigo_dms *d);
bool indigo_dms_select_convo(indigo_dms *d, unsigned index);
bool indigo_dms_move_convo(indigo_dms *d, int delta, unsigned rows);
const indigo_convo *indigo_dms_selected(const indigo_dms *d);
const indigo_convo *indigo_dms_find(const indigo_dms *d, const char *id);
bool indigo_dms_append_message(indigo_dms *d, const indigo_message *m);
bool indigo_dms_wants_page(const indigo_dms *d);
bool indigo_dms_append_convo(indigo_dms *d, const indigo_convo *c);

#endif
