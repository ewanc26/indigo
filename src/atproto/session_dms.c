/* Direct messages: listing conversations, fetching messages, sending a message. */

#include "atproto/session_internal.h"

#if defined(__3DS__) && defined(WOLFRAM_3DS)

#include <wolfram/chat_typed.h>

static void
fill_convo(const wf_chat_convo *wc, indigo_convo *o)
{
    memset(o, 0, sizeof *o);
    indigo_copy_utf8(o->id, sizeof o->id, wc->id);

    /* Pick the recipient: the first member that is not the signed-in account. */
    const char *my_did = g_session.agent ? wf_agent_get_did(g_session.agent) : NULL;
    const wf_agent_profile_view *peer = NULL;
    for (size_t i = 0; i < wc->member_count && !peer; i++) {
        const wf_agent_profile_view *m = &wc->members[i];
        if (m->did && (!my_did || strcmp(m->did, my_did) != 0)) {
            peer = m;
        }
    }
    if (!peer && wc->member_count > 0) {
        peer = &wc->members[0];
    }

    if (peer) {
        indigo_copy_utf8(o->handle, sizeof o->handle, peer->handle);
        indigo_copy_utf8(o->name, sizeof o->name,
                         peer->display_name && peer->display_name[0] ? peer->display_name
                                                                     : (peer->handle ? peer->handle : ""));
        if (peer->avatar) {
            indigo_media_cdn_url(o->avatar, sizeof o->avatar, peer->avatar, INDIGO_CDN_AVATAR);
        }
        if (my_did && wc->member_count == 2) {
            o->kind = INDIGO_CONVO_OTHER;
        }
    }

    if (wc->last_message_text) {
        indigo_copy_utf8(o->last_text, sizeof o->last_text, wc->last_message_text);
    }
    o->unread = wc->has_unread_count ? (unsigned) wc->unread_count : 0;
    o->has_unread = wc->has_unread_count;
}

void
indigo_session_do_convos(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_CONVOS_FAILED};
    wf_chat_convo_list list;
    wf_status st;

    INDIGO_SESSION_AGENT_GUARD(ev);

    memset(&list, 0, sizeof list);
    const char *cursor = j->paging && j->cursor[0] ? j->cursor : NULL;
    st = wf_agent_chat_list_convos(g_session.agent, INDIGO_CONVO_MAX, cursor, &list);
    if (st != WF_OK) {
        indigo_session_publish_failure(&ev, "listConvos", st);
        return;
    }

    if (!j->paging) {
        g_session.convo_count = 0;
    }
    for (size_t i = 0; i < list.convo_count && g_session.convo_count < INDIGO_CONVO_MAX;
         i++) {
        fill_convo(&list.convos[i], &g_session.convos[g_session.convo_count]);
        g_session.convo_count++;
    }
    if (list.cursor) {
        indigo_copy_utf8(ev.cursor, sizeof ev.cursor, list.cursor);
    }

    wf_chat_convo_list_free(&list);

    ev.kind = INDIGO_SESSION_EVENT_CONVOS_PAGE;
    ev.convo_count = g_session.convo_count;
    indigo_log_info("convos: %u", g_session.convo_count);
    indigo_session_publish_event(&ev);
}

void
indigo_session_do_send_message(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_MESSAGE_FAILED};
    wf_chat_message result;
    wf_status st;

    INDIGO_SESSION_AGENT_GUARD(ev);

    memset(&result, 0, sizeof result);
    st = wf_agent_chat_send_message(g_session.agent, j->convo_id, j->message_text,
                                    NULL, &result);
    if (st != WF_OK) {
        indigo_session_publish_failure(&ev, "sendMessage", st);
        return;
    }

    ev.kind = INDIGO_SESSION_EVENT_MESSAGE_SENT;
    ev.sent = true;
    indigo_copy_utf8(ev.message, sizeof ev.message, result.text ? result.text : "");
    wf_chat_message_reset(&result);
    indigo_session_publish_event(&ev);
}

void
indigo_session_do_messages(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_MESSAGES_FAILED};
    wf_chat_message_list list;
    wf_status st;

    INDIGO_SESSION_AGENT_GUARD(ev);

    memset(&list, 0, sizeof list);
    const char *cursor = j->paging && j->cursor[0] ? j->cursor : NULL;
    st = wf_agent_chat_get_messages(g_session.agent, j->convo_messages_id,
                                    INDIGO_MESSAGE_MAX, cursor, &list);
    if (st != WF_OK) {
        indigo_session_publish_failure(&ev, "getMessages", st);
        return;
    }

    if (!j->paging) {
        g_session.message_count = 0;
    }
    for (size_t i = 0; i < list.message_count && g_session.message_count < INDIGO_MESSAGE_MAX;
         i++) {
        const wf_chat_message *wm = &list.messages[i];
        indigo_message *o = &g_session.messages[g_session.message_count];

        memset(o, 0, sizeof *o);
        indigo_copy_utf8(o->id, sizeof o->id, wm->id);
        indigo_copy_utf8(o->sender, sizeof o->sender, wm->sender);
        indigo_copy_utf8(o->text, sizeof o->text, wm->text);
        indigo_copy_utf8(o->sent_at, sizeof o->sent_at, wm->sent_at);

        const char *my_did = g_session.agent ? wf_agent_get_did(g_session.agent) : NULL;
        o->is_mine = (my_did && wm->sender && strcmp(wm->sender, my_did) == 0);
        g_session.message_count++;
    }

    if (list.cursor) {
        indigo_copy_utf8(ev.cursor, sizeof ev.cursor, list.cursor);
    }
    wf_chat_message_list_free(&list);

    ev.kind = INDIGO_SESSION_EVENT_MESSAGES_PAGE;
    ev.page_count = g_session.message_count;
    indigo_log_info("messages: %u", g_session.message_count);
    indigo_session_publish_event(&ev);
}

#endif /* 3DS with Wolfram */
