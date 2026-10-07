/* Publishing a post, reply or quote. */

#include "atproto/session_internal.h"

#if defined(__3DS__) && defined(WOLFRAM_3DS)

void
indigo_session_do_publish(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_PUBLISH_FAILED,
                               .compose_mode = j->mode};
    wf_agent_post_result res = {0};
    wf_status st = WF_ERR_INVALID_ARG;
    cJSON *images = NULL;
    char *embed_json = NULL;

    if (!g_session.agent) {
        ev.failure = WF_FAIL_NOT_READY;
        indigo_session_publish_event(&ev);
        return;
    }
    if (j->image_path[0]) {
        st = wf_agent_upload_image_file(g_session.agent, j->image_path, j->image_alt, &images);
        if (st != WF_OK) {
            /* Nothing is posted without the picture the person chose. */
            indigo_log_warn("image upload failed: wolfram status %d", (int) st);
            ev.failure = wf_failure_classify(st, 0, NULL);
            indigo_session_publish_event(&ev);
            return;
        }
    }
    if (images) {
        embed_json = cJSON_PrintUnformatted(images);
        if (!embed_json) {
            cJSON_Delete(images);
            ev.failure = WF_FAIL_OTHER;
            indigo_session_publish_event(&ev);
            return;
        }
    }
    switch (j->mode) {
    case INDIGO_COMPOSE_POST:
        st = images ? wf_agent_post_with_embed(g_session.agent, j->text, embed_json, &res)
                    : wf_agent_post(g_session.agent, j->text, &res);
        break;
    case INDIGO_COMPOSE_REPLY:
        st = images ? wf_agent_reply_refs_with_embed(g_session.agent, j->text, j->root_uri, j->root_cid,
                                                     j->post_uri, j->post_cid, embed_json, &res)
                    : wf_agent_reply_refs(g_session.agent, j->text, j->root_uri, j->root_cid, j->post_uri,
                                          j->post_cid, &res);
        break;
    case INDIGO_COMPOSE_QUOTE:
        st = images ? wf_agent_quote_with_media(g_session.agent, j->text, j->post_uri, j->post_cid, images,
                                                &res)
                    : wf_agent_quote(g_session.agent, j->text, j->post_uri, j->post_cid, &res);
        break;
    }
    free(embed_json);
    cJSON_Delete(images);
    if (st == WF_OK) {
        ev.kind = INDIGO_SESSION_EVENT_PUBLISHED;
        if (res.uri && strlen(res.uri) < sizeof ev.record_uri) {
            snprintf(ev.record_uri, sizeof ev.record_uri, "%s", res.uri);
        }
        indigo_log_info("published (mode %d)", (int) j->mode);
        /* Reply gate, new top-level posts only: a threadgate attaches to the
         * post it names, and gating a reply separately from its thread is not
         * something the official client offers either. The rules are Wolfram's.
         *
         * A failure here does not roll the post back. It exists, ungated,
         * which is the safer outcome than dropping something the user can see
         * themselves have posted. */
        if (j->mode == INDIGO_COMPOSE_POST && j->reply_gate > 0 && res.uri && res.uri[0]) {
            wf_status gst = wf_agent_set_reply_gate(g_session.agent, res.uri, (wf_reply_gate) j->reply_gate);

            if (gst != WF_OK) {
                indigo_log_warn("reply gate failed (wolfram status %d) for %s", (int) gst, res.uri);
            } else {
                indigo_log_info("reply gate set on %s", res.uri);
            }
        }
    } else {
        ev.failure = wf_failure_classify(st, 0, NULL);
        indigo_log_warn("publish failed: wolfram status %d (%s)", (int) st,
                        wf_failure_tag(ev.failure));
    }
    wf_agent_post_result_free(&res);
    indigo_session_publish_event(&ev);
}

bool
indigo_session_submit_publish(indigo_compose_mode mode, const char *text,
                              const char *target_uri, const char *target_cid,
                              const char *root_uri, const char *root_cid, int reply_gate,
                              const char *image_path, const char *image_alt)
{
    indigo_job j = {.kind = JOB_PUBLISH, .mode = mode, .reply_gate = reply_gate};

    snprintf(j.text, sizeof j.text, "%s", text);
    snprintf(j.image_path, sizeof j.image_path, "%s", image_path ? image_path : "");
    snprintf(j.image_alt, sizeof j.image_alt, "%s", image_alt ? image_alt : "");
    snprintf(j.post_uri, sizeof j.post_uri, "%s", target_uri ? target_uri : "");
    snprintf(j.post_cid, sizeof j.post_cid, "%s", target_cid ? target_cid : "");
    snprintf(j.root_uri, sizeof j.root_uri, "%s", root_uri ? root_uri : "");
    snprintf(j.root_cid, sizeof j.root_cid, "%s", root_cid ? root_cid : "");
    return indigo_session_enqueue(&j);
}

#endif /* 3DS with Wolfram */
