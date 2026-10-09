/* Publishing a post, reply or quote, and deleting your own post. */

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

/* A post URI names the repository DID before the collection. Do not send a
 * delete for another account or for a different record collection. */
static bool
post_uri_is_own(const char *uri)
{
    const char *did = g_session.agent ? wf_agent_get_did(g_session.agent) : NULL;
    const char *authority;
    const char *slash;
    static const char collection[] = "/app.bsky.feed.post/";
    size_t did_len;

    if (!uri || !did || !did[0] || strncmp(uri, "at://", 5) != 0) {
        return false;
    }
    authority = uri + 5;
    slash = strchr(authority, '/');
    did_len = strlen(did);
    return slash && (size_t) (slash - authority) == did_len &&
           memcmp(authority, did, did_len) == 0 &&
           strncmp(slash, collection, sizeof collection - 1) == 0 &&
           slash[sizeof collection - 1] != '\0';
}

void
indigo_session_do_delete_post(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_POST_DELETE_FAILED};
    wf_status st;

    snprintf(ev.post_uri, sizeof ev.post_uri, "%s", j->post_uri);
    if (!g_session.agent) {
        ev.failure = WF_FAIL_NOT_READY;
        indigo_session_publish_event(&ev);
        return;
    }
    if (!post_uri_is_own(j->post_uri)) {
        ev.failure = WF_FAIL_OTHER;
        indigo_log_warn("refusing to delete a post outside the signed-in repository");
        indigo_session_publish_event(&ev);
        return;
    }

    st = wf_agent_delete_post(g_session.agent, j->post_uri);
    if (st != WF_OK) {
        ev.failure = wf_failure_classify(st, 0, NULL);
        indigo_log_warn("post delete failed: wolfram status %d (%s)", (int) st,
                        wf_failure_tag(ev.failure));
    } else {
        ev.kind = INDIGO_SESSION_EVENT_POST_DELETED;
        ev.failure = WF_FAIL_NONE;
        indigo_log_info("post deleted");
    }
    indigo_session_publish_event(&ev);
}

bool
indigo_session_submit_delete_post(const char *post_uri)
{
    indigo_job j = {.kind = JOB_DELETE_POST};

    if (!post_uri || !post_uri[0]) {
        return false;
    }
    snprintf(j.post_uri, sizeof j.post_uri, "%s", post_uri);
    return indigo_session_enqueue(&j);
}

#endif /* 3DS with Wolfram */
