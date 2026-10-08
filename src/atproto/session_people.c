/* People: search, a profile, follow, block and mute, and the followers, following and likes lists. */

#include "atproto/session_internal.h"

#if defined(__3DS__) && defined(WOLFRAM_3DS)

void
indigo_session_do_profile(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_PROFILE_FAILED};
    wf_agent_profile p;
    wf_status st;

    INDIGO_SESSION_AGENT_GUARD(ev);
    memset(&p, 0, sizeof p);
    st = wf_agent_get_profile(g_session.agent, j->post_uri, &p);
    if (st != WF_OK) {
        indigo_session_publish_failure(&ev, "profile", st);
        return;
    }
    memset(&g_session.profile, 0, sizeof g_session.profile);
    indigo_copy_utf8(g_session.profile.handle, sizeof g_session.profile.handle, p.handle);
    indigo_copy_utf8(g_session.profile.display_name, sizeof g_session.profile.display_name, p.display_name);
    indigo_media_cdn_url(g_session.profile.avatar, sizeof g_session.profile.avatar, p.avatar, INDIGO_CDN_AVATAR);
    indigo_copy_utf8(g_session.profile.bio, sizeof g_session.profile.bio, p.description);
    indigo_copy_utf8(g_session.profile.did, sizeof g_session.profile.did, p.did);
    indigo_copy_utf8(g_session.profile.follow_uri, sizeof g_session.profile.follow_uri, p.following);
    indigo_copy_utf8(g_session.profile.block_uri, sizeof g_session.profile.block_uri, p.blocking);
    indigo_copy_utf8(g_session.profile.pinned_uri, sizeof g_session.profile.pinned_uri, p.pinned_post_uri);
    g_session.profile.muted = p.muted;
    g_session.profile.blocked = p.blocking != NULL;
    g_session.profile.followers = indigo_session_count_of(p.followers_count);
    g_session.profile.follows = indigo_session_count_of(p.follows_count);
    g_session.profile.posts = indigo_session_count_of(p.posts_count);
    g_session.profile.following = p.following != NULL;
    g_session.profile.loaded = true;
    wf_agent_profile_free(&p);
    ev.kind = INDIGO_SESSION_EVENT_PROFILE_LOADED;
    indigo_session_publish_event(&ev);
}

/* Followers, following, likes and search all end the same way: the page lands in
 * the one actor array (a fresh list replaces it, a further page appends) and the
 * event carries the count and the next cursor. */
void
indigo_session_publish_actor_page(const indigo_job *j, wf_agent_actor_list *list, indigo_session_event *ev)
{
    INDIGO_SESSION_PAGE_RESET(j->paging);
    for (size_t i = 0; i < list->actor_count && g_session.actor_count < INDIGO_SEARCH_MAX; i++) {
        if (indigo_session_fill_actor(&list->actors[i], &g_session.actors[g_session.actor_count])) {
            g_session.actor_count++;
        }
    }
    ev->kind = INDIGO_SESSION_EVENT_SEARCH_PAGE;
    ev->page_count = g_session.actor_count;
    indigo_copy_utf8(ev->cursor, sizeof ev->cursor, list->cursor ? list->cursor : "");
    wf_agent_actor_list_free(list);
}

void
indigo_session_do_search(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_SEARCH_FAILED};
    wf_agent_actor_list list;
    wf_status st;
    const char *cursor = INDIGO_SESSION_PAGE_CURSOR(*j);

    INDIGO_SESSION_AGENT_GUARD(ev);
    if (!j->query[0]) {
        /* The app refuses to submit an empty query, so reaching this is a bug
         * rather than something a person did. */
        ev.failure = WF_FAIL_OTHER;
        indigo_session_publish_event(&ev);
        return;
    }
    memset(&list, 0, sizeof list);
    st = wf_agent_search_actors_typed(g_session.agent, j->query, INDIGO_SEARCH_PAGE,
                                      cursor, &list);
    if (st != WF_OK) {
        indigo_session_publish_failure(&ev, "search", st);
        return;
    }

    indigo_session_publish_actor_page(j, &list, &ev);
    indigo_log_info("search '%s': %u", j->query, g_session.actor_count);
    indigo_session_publish_event(&ev);
}

/* Followers and following return the same actor view as searchActors, and the
 * screen shows one list at a time, so the results land in the same array and
 * report through the same events. Only the request differs. */
void
indigo_session_do_people(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_SEARCH_FAILED};
    wf_agent_actor_list list;
    wf_status st = WF_ERR_INVALID_ARG;
    const char *cursor = INDIGO_SESSION_PAGE_CURSOR(*j);

    INDIGO_SESSION_AGENT_GUARD(ev);
    memset(&list, 0, sizeof list);
    if (j->people_kind == INDIGO_SEARCH_LIKED_BY) {
        wf_agent_like_list likes;

        memset(&likes, 0, sizeof likes);
        st = wf_feedgen_get_likes_typed(g_session.agent, j->list_uri, NULL, INDIGO_SEARCH_PAGE,
                                        cursor, &likes);
        if (st != WF_OK) {
            indigo_session_publish_failure(&ev, "liked-by", st);
            return;
        }
        if (!j->paging) {
            g_session.actor_count = 0;
        }
        for (size_t i = 0; i < likes.like_count && g_session.actor_count < INDIGO_SEARCH_MAX; i++) {
            if (indigo_session_fill_actor(&likes.likes[i].actor, &g_session.actors[g_session.actor_count])) {
                g_session.actor_count++;
            }
        }
        ev.kind = INDIGO_SESSION_EVENT_SEARCH_PAGE;
        ev.page_count = g_session.actor_count;
        indigo_copy_utf8(ev.cursor, sizeof ev.cursor, likes.cursor ? likes.cursor : "");
        wf_agent_like_list_free(&likes);
        indigo_log_info("liked-by: %u", g_session.actor_count);
        indigo_session_publish_event(&ev);
        return;
    }
    if (j->people_kind == INDIGO_SEARCH_REPOSTED_BY) {
        st = wf_feedgen_get_reposted_by_typed(g_session.agent, j->list_uri, NULL,
                                              INDIGO_SEARCH_PAGE, cursor, &list);
    } else if (j->people_kind == INDIGO_SEARCH_FOLLOWERS) {
        st = wf_agent_get_followers_typed(g_session.agent, j->actor, INDIGO_SEARCH_PAGE,
                                           cursor, &list);
    } else {
        st = wf_agent_get_follows_typed(g_session.agent, j->actor, INDIGO_SEARCH_PAGE,
                                         cursor, &list);
    }
    if (st != WF_OK) {
        indigo_session_publish_failure(&ev, "people", st);
        return;
    }

    indigo_session_publish_actor_page(j, &list, &ev);
    indigo_log_info("people %d '%s': %u", (int) j->people_kind, j->actor, g_session.actor_count);
    indigo_session_publish_event(&ev);
}

void
indigo_session_do_follow(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_FOLLOW_FAILED,
                               .follow = j->follow};
    wf_agent_post_result res = {0};
    wf_status st = WF_ERR_INVALID_ARG;

    snprintf(ev.actor, sizeof ev.actor, "%s", j->actor);
    INDIGO_SESSION_AGENT_GUARD(ev);
    switch (j->follow) {
    case INDIGO_FOLLOW:
        st = wf_agent_follow(g_session.agent, j->actor, &res);
        break;
    case INDIGO_UNFOLLOW:
        if (!j->undo_uri[0]) {
            /* The app only offers unfollow while it holds the record URI, so
             * an empty one here is a bug rather than a person tapping it. */
            ev.failure = WF_FAIL_OTHER;
            break;
        }
        st = wf_agent_unfollow(g_session.agent, j->undo_uri);
        break;
    case INDIGO_FOLLOW_NONE:
        break;
    }
    if (st == WF_OK) {
        ev.kind = INDIGO_SESSION_EVENT_FOLLOW_DONE;
        if (res.uri && strlen(res.uri) < sizeof ev.record_uri) {
            snprintf(ev.record_uri, sizeof ev.record_uri, "%s", res.uri);
        }
        /* The server owns the follower count; the app adjusts its copy from
         * here rather than assuming the write already landed. */
        if (j->follow == INDIGO_FOLLOW && g_session.profile.followers < UINT_MAX) {
            g_session.profile.followers++;
        } else if (j->follow == INDIGO_UNFOLLOW && g_session.profile.followers > 0) {
            g_session.profile.followers--;
        }
    } else {
        ev.failure = wf_failure_classify(st, 0, NULL);
        indigo_log_warn("follow %d failed: wolfram status %d (%s)", (int) j->follow, (int) st,
                        wf_failure_tag(ev.failure));
    }
    wf_agent_post_result_free(&res);
    indigo_session_publish_event(&ev);
}

void
indigo_session_do_graph(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_GRAPH_FAILED, .graph = j->graph};
    wf_agent_post_result res = {0};
    wf_status st = WF_ERR_INVALID_ARG;

    snprintf(ev.actor, sizeof ev.actor, "%s", j->actor);
    INDIGO_SESSION_AGENT_GUARD(ev);
    switch (j->graph) {
    case INDIGO_GRAPH_MUTE:
        st = wf_agent_mute_actor(g_session.agent, j->actor);
        break;
    case INDIGO_GRAPH_UNMUTE:
        st = wf_agent_unmute_actor(g_session.agent, j->actor);
        break;
    case INDIGO_GRAPH_BLOCK:
        st = wf_agent_block(g_session.agent, j->actor, &res);
        break;
    case INDIGO_GRAPH_UNBLOCK:
        /* Block is a repo record, so unblocking deletes by URI and there is
         * no handle to resolve one from. */
        if (!j->undo_uri[0]) {
            ev.failure = WF_FAIL_OTHER;
            break;
        }
        st = wf_agent_unblock(g_session.agent, j->undo_uri);
        break;
    case INDIGO_GRAPH_NONE:
        break;
    }
    if (st == WF_OK) {
        ev.kind = INDIGO_SESSION_EVENT_GRAPH_DONE;
        if (res.uri && strlen(res.uri) < sizeof ev.record_uri) {
            snprintf(ev.record_uri, sizeof ev.record_uri, "%s", res.uri);
        }
    } else {
        ev.failure = wf_failure_classify(st, 0, NULL);
        indigo_log_warn("graph action %d failed: wolfram status %d (%s)", (int) j->graph,
                        (int) st, wf_failure_tag(ev.failure));
    }
    wf_agent_post_result_free(&res);
    indigo_session_publish_event(&ev);
}

bool
indigo_session_submit_profile(const char *actor)
{
    indigo_job j = {.kind = JOB_PROFILE};

    snprintf(j.post_uri, sizeof j.post_uri, "%s", actor);
    return indigo_session_enqueue(&j);
}

const indigo_profile *
indigo_session_profile(void)
{
    return &g_session.profile;
}

bool
indigo_session_submit_search(const char *query, bool paging)
{
    indigo_job j = {.kind = JOB_SEARCH};

    if (!query) {
        return false;
    }
    j.paging = paging;
    indigo_copy_utf8(j.query, sizeof j.query, query);
    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_follow(indigo_follow_action action, const char *did,
                             const char *follow_uri)
{
    indigo_job j = {.kind = JOB_FOLLOW, .follow = action};

    if (action == INDIGO_FOLLOW_NONE || !did || !did[0]) {
        return false;
    }
    if (action == INDIGO_UNFOLLOW && (!follow_uri || !follow_uri[0])) {
        return false;
    }
    indigo_copy_utf8(j.actor, sizeof j.actor, did);
    indigo_copy_utf8(j.undo_uri, sizeof j.undo_uri, follow_uri ? follow_uri : "");
    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_graph(indigo_graph_action action, const char *did,
                            const char *block_uri)
{
    indigo_job j = {.kind = JOB_GRAPH, .graph = action};

    if (action == INDIGO_GRAPH_NONE || !did || !did[0]) {
        return false;
    }
    if (action == INDIGO_GRAPH_UNBLOCK && (!block_uri || !block_uri[0])) {
        return false;
    }
    indigo_copy_utf8(j.actor, sizeof j.actor, did);
    indigo_copy_utf8(j.undo_uri, sizeof j.undo_uri, block_uri ? block_uri : "");
    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_people(indigo_search_kind kind, const char *subject, bool paging)
{
    indigo_job j = {.kind = JOB_PEOPLE, .people_kind = kind};

    if (kind == INDIGO_SEARCH_PEOPLE || !subject || !subject[0]) {
        return false;
    }
    j.paging = paging;
    /* A post's URI is longer than a handle, so the two kinds that take one
     * keep it in the URI-sized field rather than truncating it into actor. */
    if (kind == INDIGO_SEARCH_LIKED_BY || kind == INDIGO_SEARCH_REPOSTED_BY) {
        indigo_copy_utf8(j.list_uri, sizeof j.list_uri, subject);
    } else {
        indigo_copy_utf8(j.actor, sizeof j.actor, subject);
    }
    return indigo_session_enqueue(&j);
}

const indigo_actor *
indigo_session_search_results(unsigned *count)
{
    *count = g_session.actor_count;
    return g_session.actors;
}

#endif /* 3DS with Wolfram */
