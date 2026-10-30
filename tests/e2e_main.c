/*
 * Host end-to-end: the real session worker, its queue and the group jobs run
 * against a mock PDS on 127.0.0.1. `make e2e` builds this with INDIGO_SESSION_HOST
 * and a pthread-backed <3ds.h>; nothing here runs on the console.
 *
 * The group scenarios go through the same submit calls the screens use, so the
 * member resolution, the request bodies and the state the group screen reads
 * are all exercised, not a copy of them.
 */

#include "atproto/session.h"
#include "e2e/mock_pds.h"
#include "util/log.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int checks;
static int failures;

#define CHECK(c)                                                          \
    do {                                                                  \
        checks++;                                                         \
        if (!(c)) {                                                       \
            failures++;                                                   \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);            \
        }                                                                 \
    } while (0)

#define CHECK_STR(a, b)                                                   \
    do {                                                                  \
        checks++;                                                         \
        if (strcmp((a), (b)) != 0) {                                      \
            failures++;                                                   \
            printf("FAIL %s:%d \"%s\" != \"%s\"\n", __FILE__, __LINE__,   \
                   (a), (b));                                             \
        }                                                                 \
    } while (0)

/* Poll the worker for its next event. The timeout is generous because the
 * first request of a run may wait on DID resolution. */
static bool
wait_event(indigo_session_event *out)
{
    for (int i = 0; i < 2000; i++) {
        if (indigo_session_poll(out)) {
            return true;
        }
        usleep(5000);
    }
    return false;
}

static bool
wait_for_kind(indigo_session_event_kind kind, indigo_session_event *out)
{
    if (!wait_event(out)) {
        return false;
    }
    if (out->kind != kind) {
        printf("FAIL expected event %d, got %d (failure %d: %s)\n",
               (int) kind, (int) out->kind, (int) out->failure, out->message);
        return false;
    }
    return true;
}

/* The createGroup/getConvo answer: a group with three members and an optional
 * join link. */
static const char *
group_view_json(const char *id, const char *name, const char *code,
                const char *status)
{
    static char buf[4][2048];
    static int k;
    char *b = buf[k++ & 3];
    char link[256] = "";

    if (code) {
        snprintf(link, sizeof link,
                 ",\"joinLink\":{\"code\":\"%s\",\"enabledStatus\":\"%s\","
                 "\"requireApproval\":false,\"joinRule\":\"anyone\"}",
                 code, status ? status : "enabled");
    }
    snprintf(b, 2048,
             "{\"convo\":{\"id\":\"%s\",\"rev\":\"1\",\"unreadCount\":0,"
             "\"members\":[{\"did\":\"did:plc:abc\",\"handle\":\"ewancroft.uk\"},"
             "{\"did\":\"did:plc:bob\",\"handle\":\"bob.test\",\"displayName\":\"bob\"},"
             "{\"did\":\"did:plc:carol\",\"handle\":\"carol.test\","
             "\"displayName\":\"carol\"}],"
             "\"kind\":{\"$type\":\"chat.bsky.convo.defs#groupConvo\",\"name\":\"%s\","
             "\"memberCount\":3,\"memberLimit\":50,\"joinRequestCount\":0%s}}}",
             id, name, link);
    return b;
}

static const char *
group_members_json(void)
{
    return "{\"members\":["
           "{\"did\":\"did:plc:abc\",\"handle\":\"ewancroft.uk\"},"
           "{\"did\":\"did:plc:bob\",\"handle\":\"bob.test\",\"displayName\":\"bob\"},"
           "{\"did\":\"did:plc:carol\",\"handle\":\"carol.test\","
           "\"displayName\":\"carol\"}]}";
}

static const char *
join_link_json(const char *status)
{
    static char buf[2][256];
    static int k;
    char *b = buf[k++ & 1];

    snprintf(b, 256,
             "{\"joinLink\":{\"code\":\"join-code\",\"enabledStatus\":\"%s\","
             "\"requireApproval\":false,\"joinRule\":\"anyone\","
             "\"createdAt\":\"2026-10-01T10:00:00.000Z\"}}",
             status);
    return b;
}

/* The join-request answer: two people asking to join g1. */
static const char *
join_requests_two(void)
{
    return "{\"requests\":["
           "{\"convoId\":\"g1\",\"requestedBy\":{\"did\":\"did:plc:bob\","
           "\"handle\":\"bob.test\",\"displayName\":\"bob\"},"
           "\"requestedAt\":\"2026-10-01T10:00:00.000Z\"},"
           "{\"convoId\":\"g1\",\"requestedBy\":{\"did\":\"did:plc:dave\","
           "\"handle\":\"dave.test\",\"displayName\":\"dave\"},"
           "\"requestedAt\":\"2026-10-01T10:00:00.000Z\"}]}";
}

static void
test_group_create_and_join_link(const char *data_dir)
{
    e2e_pds *pds = NULL;
    int port = 0;
    char svc[64];
    char session_path[512];
    indigo_session_event ev;

    CHECK(e2e_pds_start(&pds, &port));
    if (!pds) {
        return;
    }
    snprintf(svc, sizeof svc, "http://127.0.0.1:%d", port);
    snprintf(session_path, sizeof session_path, "%s/session.dat", data_dir);
    unlink(session_path);

    e2e_pds_route(pds, "com.atproto.server.createSession", 200,
                  "{\"did\":\"did:plc:abc\",\"handle\":\"ewancroft.uk\","
                  "\"accessJwt\":\"a.b.c\",\"refreshJwt\":\"d.e.f\",\"active\":true}");
    e2e_pds_route(pds, "com.atproto.identity.resolveHandle", 200,
                  "{\"did\":\"did:plc:alice\"}");
    e2e_pds_route(pds, "chat.bsky.group.createGroup", 200,
                  group_view_json("g1", "Weekend crew", NULL, NULL));
    e2e_pds_route(pds, "chat.bsky.convo.getConvo", 200,
                  group_view_json("g1", "Weekend crew", NULL, NULL));
    e2e_pds_route(pds, "chat.bsky.convo.getConvoMembers", 200,
                  group_members_json());
    e2e_pds_route(pds, "chat.bsky.group.createJoinLink", 200,
                  join_link_json("enabled"));
    e2e_pds_route(pds, "chat.bsky.group.disableJoinLink", 200,
                  join_link_json("disabled"));
    e2e_pds_route(pds, "chat.bsky.group.enableJoinLink", 200,
                  join_link_json("enabled"));

    CHECK(indigo_session_start(session_path));
    CHECK(indigo_session_submit_login(svc, "ewancroft.test", "app-pass"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SIGNED_IN, &ev));

    /* An entry that is neither a handle nor a DID stops before any request,
     * and names the entry back. */
    const int creates_before = e2e_pds_hits(pds, "chat.bsky.group.createGroup");
    CHECK(indigo_session_submit_chat_group_create("Weekend crew", "not a handle"));
    CHECK(wait_event(&ev));
    CHECK(ev.kind == INDIGO_SESSION_EVENT_CHAT_GROUP_CREATE_FAILED);
    CHECK(strstr(ev.message, "not a handle") != NULL);
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.createGroup") == creates_before);

    /* The handle resolves to a DID; the viewer and a duplicate are dropped,
     * and the request carries DIDs rather than the text that was typed. */
    const int resolves_before = e2e_pds_hits(pds, "com.atproto.identity.resolveHandle");
    CHECK(indigo_session_submit_chat_group_create(
        "Weekend crew", "@alice.test, did:plc:bob, did:plc:abc, did:plc:bob"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_GROUP_CREATE_DONE, &ev));
    CHECK(ev.page_count == 3);
    CHECK(e2e_pds_hits(pds, "com.atproto.identity.resolveHandle") ==
          resolves_before + 1);
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.createGroup") ==
          creates_before + 1);
    CHECK(e2e_pds_request_contains(pds, "chat.bsky.group.createGroup",
        "{\"members\":[\"did:plc:alice\",\"did:plc:bob\"],\"name\":\"Weekend crew\"}"));
    CHECK(!e2e_pds_request_contains(pds, "chat.bsky.group.createGroup",
        "alice.test"));
    CHECK(!e2e_pds_request_contains(pds, "chat.bsky.group.createGroup",
        "did:plc:abc"));
    CHECK_STR(indigo_session_chat_group_started(), "g1");

    const indigo_chat_group *group = indigo_session_chat_group();
    CHECK(group->loaded);
    CHECK_STR(group->name, "Weekend crew");
    CHECK(group->total == 3);
    CHECK(group->count == 3);
    CHECK(group->join_link_code[0] == '\0');
    CHECK(group->join_link_url[0] == '\0');
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getConvo") == 1);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getConvoMembers") == 1);

    /* A failed create leaves no link and does not read the group back. */
    e2e_pds_route(pds, "chat.bsky.group.createJoinLink", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_chat_join_link_create("g1"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_JOIN_LINK_FAILED, &ev));
    CHECK(ev.failure != WF_FAIL_NONE);
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.createJoinLink") == 1);
    group = indigo_session_chat_group();
    CHECK(!group->join_link_enabled);
    CHECK(group->join_link_code[0] == '\0');
    CHECK(group->join_link_url[0] == '\0');
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getConvo") == 1);

    /* Creating the link makes it, reads the group back and shows the public URL. */
    e2e_pds_route(pds, "chat.bsky.group.createJoinLink", 200,
                  join_link_json("enabled"));
    e2e_pds_route(pds, "chat.bsky.convo.getConvo", 200,
                  group_view_json("g1", "Weekend crew", "join-code", "enabled"));
    CHECK(indigo_session_submit_chat_join_link_create("g1"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_JOIN_LINK_DONE, &ev));
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.createJoinLink") == 2);
    group = indigo_session_chat_group();
    CHECK(group->join_link_enabled);
    CHECK_STR(group->join_link_code, "join-code");
    CHECK_STR(group->join_link_url, "https://bsky.app/chat/join-code");
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getConvo") == 2);

    /* Disabling toggles the existing link; it never creates a second one. */
    e2e_pds_route(pds, "chat.bsky.convo.getConvo", 200,
                  group_view_json("g1", "Weekend crew", "join-code", "disabled"));
    CHECK(indigo_session_submit_chat_join_link_toggle("g1", true));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_JOIN_LINK_DONE, &ev));
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.disableJoinLink") == 1);
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.createJoinLink") == 2);
    group = indigo_session_chat_group();
    CHECK(!group->join_link_enabled);
    CHECK_STR(group->join_link_url, "https://bsky.app/chat/join-code");
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getConvo") == 3);

    /* A failed toggle keeps the last known state and is not retried. */
    e2e_pds_route(pds, "chat.bsky.group.enableJoinLink", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_chat_join_link_toggle("g1", false));
    CHECK(wait_event(&ev));
    CHECK(ev.kind == INDIGO_SESSION_EVENT_CHAT_JOIN_LINK_FAILED);
    group = indigo_session_chat_group();
    CHECK(!group->join_link_enabled);
    CHECK_STR(group->join_link_code, "join-code");
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.enableJoinLink") == 1);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getConvo") == 3);

    /* The next press enables it, one request, and the URL is unchanged. */
    e2e_pds_route(pds, "chat.bsky.group.enableJoinLink", 200,
                  join_link_json("enabled"));
    e2e_pds_route(pds, "chat.bsky.convo.getConvo", 200,
                  group_view_json("g1", "Weekend crew", "join-code", "enabled"));
    CHECK(indigo_session_submit_chat_join_link_toggle("g1", false));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_JOIN_LINK_DONE, &ev));
    group = indigo_session_chat_group();
    CHECK(group->join_link_enabled);
    CHECK_STR(group->join_link_url, "https://bsky.app/chat/join-code");
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.enableJoinLink") == 2);

    indigo_session_stop();
    e2e_pds_free(pds);
}

/* A group's settings and pending join requests, from the detail fetch through
 * rename, add and remove to the two decisions. Every request goes through the
 * same submit calls the screens use. */
static void
test_group_settings_and_join_requests(const char *data_dir)
{
    e2e_pds *pds = NULL;
    int port = 0;
    char svc[64];
    char session_path[512];
    indigo_session_event ev;

    CHECK(e2e_pds_start(&pds, &port));
    if (!pds) {
        return;
    }
    snprintf(svc, sizeof svc, "http://127.0.0.1:%d", port);
    snprintf(session_path, sizeof session_path, "%s/session-members.dat", data_dir);
    unlink(session_path);

    e2e_pds_route(pds, "com.atproto.server.createSession", 200,
                  "{\"did\":\"did:plc:abc\",\"handle\":\"ewancroft.uk\","
                  "\"accessJwt\":\"a.b.c\",\"refreshJwt\":\"d.e.f\",\"active\":true}");
    e2e_pds_route(pds, "com.atproto.identity.resolveHandle", 200,
                  "{\"did\":\"did:plc:dave\"}");
    e2e_pds_route(pds, "chat.bsky.convo.getConvo", 200,
                  group_view_json("g1", "Weekend crew", NULL, NULL));
    e2e_pds_route(pds, "chat.bsky.convo.getConvoMembers", 200,
                  group_members_json());
    e2e_pds_route(pds, "chat.bsky.group.editGroup", 200,
                  group_view_json("g1", "Weekend crew", NULL, NULL));
    e2e_pds_route(pds, "chat.bsky.group.addMembers", 200,
                  group_view_json("g1", "Weekend crew", NULL, NULL));
    e2e_pds_route(pds, "chat.bsky.group.removeMembers", 200,
                  group_view_json("g1", "Weekend crew", NULL, NULL));
    e2e_pds_route(pds, "chat.bsky.group.listJoinRequests", 200,
                  join_requests_two());
    e2e_pds_route(pds, "chat.bsky.group.approveJoinRequest", 200,
                  group_view_json("g1", "Weekend crew", NULL, NULL));
    e2e_pds_route(pds, "chat.bsky.group.rejectJoinRequest", 200, "{}");

    CHECK(indigo_session_start(session_path));
    CHECK(indigo_session_submit_login(svc, "ewancroft.test", "app-pass"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SIGNED_IN, &ev));

    /* Opening the settings reads the convo and its full member list. */
    CHECK(indigo_session_submit_chat_group_info("g1"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_GROUP_INFO_PAGE, &ev));
    CHECK(ev.page_count == 3);
    const indigo_chat_group *group = indigo_session_chat_group();
    CHECK(group->loaded);
    CHECK_STR(group->name, "Weekend crew");
    CHECK(group->count == 3);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getConvo") == 1);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getConvoMembers") == 1);

    /* A failed rename leaves the old name and does not read the group back. */
    e2e_pds_route(pds, "chat.bsky.group.editGroup", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_chat_group_rename("g1", "New name"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_GROUP_RENAME_FAILED, &ev));
    CHECK(ev.failure != WF_FAIL_NONE);
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.editGroup") == 1);
    group = indigo_session_chat_group();
    CHECK_STR(group->name, "Weekend crew");
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getConvo") == 1);

    /* Rename sends the new name and reads the group back. */
    e2e_pds_route(pds, "chat.bsky.group.editGroup", 200,
                  group_view_json("g1", "New name", NULL, NULL));
    e2e_pds_route(pds, "chat.bsky.convo.getConvo", 200,
                  group_view_json("g1", "New name", NULL, NULL));
    CHECK(indigo_session_submit_chat_group_rename("g1", "New name"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_GROUP_RENAME_DONE, &ev));
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.editGroup") == 2);
    CHECK(e2e_pds_request_contains(pds, "chat.bsky.group.editGroup", "New name"));
    group = indigo_session_chat_group();
    CHECK_STR(group->name, "New name");
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getConvo") == 2);

    /* A failed add keeps the member list as it was, and the next press works. */
    const int resolves_before = e2e_pds_hits(pds, "com.atproto.identity.resolveHandle");
    e2e_pds_route(pds, "chat.bsky.group.addMembers", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_chat_group_add("g1", "@dave.test"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_GROUP_ADD_FAILED, &ev));
    CHECK(ev.failure != WF_FAIL_NONE);
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.addMembers") == 1);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getConvoMembers") == 2);
    group = indigo_session_chat_group();
    CHECK(group->count == 3);
    CHECK(indigo_chat_group_member_index(group, "did:plc:dave") == -1);

    /* Adding a handle resolves it, then sends the DID, and the detail fetch
     * brings the new member in. */
    e2e_pds_route(pds, "chat.bsky.group.addMembers", 200,
                  group_view_json("g1", "Weekend crew", NULL, NULL));
    e2e_pds_route(pds, "chat.bsky.convo.getConvoMembers", 200,
                  "{\"members\":["
                  "{\"did\":\"did:plc:abc\",\"handle\":\"ewancroft.uk\"},"
                  "{\"did\":\"did:plc:bob\",\"handle\":\"bob.test\"},"
                  "{\"did\":\"did:plc:carol\",\"handle\":\"carol.test\"},"
                  "{\"did\":\"did:plc:dave\",\"handle\":\"dave.test\"}]}");
    CHECK(indigo_session_submit_chat_group_add("g1", "@dave.test"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_GROUP_ADD_DONE, &ev));
    CHECK(ev.page_count == 4);
    CHECK(e2e_pds_hits(pds, "com.atproto.identity.resolveHandle") ==
          resolves_before + 2);
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.addMembers") == 2);
    CHECK(e2e_pds_request_contains(pds, "chat.bsky.group.addMembers",
                                   "did:plc:dave"));
    CHECK(!e2e_pds_request_contains(pds, "chat.bsky.group.addMembers",
                                    "dave.test"));
    group = indigo_session_chat_group();
    CHECK(group->count == 4);
    CHECK(indigo_chat_group_member_index(group, "did:plc:dave") == 3);

    /* A failed remove keeps the member; the next press removes it. */
    e2e_pds_route(pds, "chat.bsky.group.removeMembers", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_chat_group_remove("g1", "did:plc:dave"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_GROUP_REMOVE_FAILED, &ev));
    CHECK(ev.failure != WF_FAIL_NONE);
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.removeMembers") == 1);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getConvoMembers") == 3);
    group = indigo_session_chat_group();
    CHECK(group->count == 4);
    CHECK(indigo_chat_group_member_index(group, "did:plc:dave") == 3);

    /* Removing the member sends the DID and reads back without it. */
    e2e_pds_route(pds, "chat.bsky.group.removeMembers", 200,
                  group_view_json("g1", "Weekend crew", NULL, NULL));
    e2e_pds_route(pds, "chat.bsky.convo.getConvoMembers", 200,
                  group_members_json());
    CHECK(indigo_session_submit_chat_group_remove("g1", "did:plc:dave"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_GROUP_REMOVE_DONE, &ev));
    CHECK(ev.page_count == 3);
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.removeMembers") == 2);
    CHECK(e2e_pds_request_contains(pds, "chat.bsky.group.removeMembers",
                                   "did:plc:dave"));
    group = indigo_session_chat_group();
    CHECK(group->count == 3);
    CHECK(indigo_chat_group_member_index(group, "did:plc:dave") == -1);

    /* The two pending requests are listed, then settled one at a time; each
     * decision removes its own row and never refetches the list. */
    CHECK(indigo_session_submit_chat_join_requests("g1", false));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_JOIN_REQUESTS_PAGE, &ev));
    const indigo_chat_join_requests *requests = indigo_session_chat_join_requests();
    CHECK(requests->count == 2);
    CHECK(strcmp(requests->items[0].did, "did:plc:bob") == 0);
    CHECK(strcmp(requests->items[0].name, "bob") == 0);
    CHECK(strcmp(requests->items[1].did, "did:plc:dave") == 0);
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.listJoinRequests") == 1);
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.approveJoinRequest") == 0);
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.rejectJoinRequest") == 0);

    /* A failed approve keeps both rows and is not retried by itself. */
    e2e_pds_route(pds, "chat.bsky.group.approveJoinRequest", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_chat_join_approve("g1", "did:plc:bob"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_JOIN_APPROVE_FAILED, &ev));
    CHECK(ev.failure != WF_FAIL_NONE);
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.approveJoinRequest") == 1);
    requests = indigo_session_chat_join_requests();
    CHECK(requests->count == 2);
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.listJoinRequests") == 1);

    /* The next press approves the row and removes it. */
    e2e_pds_route(pds, "chat.bsky.group.approveJoinRequest", 200,
                  group_view_json("g1", "Weekend crew", NULL, NULL));
    CHECK(indigo_session_submit_chat_join_approve("g1", "did:plc:bob"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_JOIN_APPROVE_DONE, &ev));
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.approveJoinRequest") == 2);
    requests = indigo_session_chat_join_requests();
    CHECK(requests->count == 1);
    CHECK(strcmp(requests->items[0].did, "did:plc:dave") == 0);

    /* A failed reject keeps the last row; the next press rejects it. */
    e2e_pds_route(pds, "chat.bsky.group.rejectJoinRequest", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_chat_join_reject("g1", "did:plc:dave"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_JOIN_REJECT_FAILED, &ev));
    CHECK(ev.failure != WF_FAIL_NONE);
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.rejectJoinRequest") == 1);
    CHECK(indigo_session_chat_join_requests()->count == 1);
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.listJoinRequests") == 1);

    e2e_pds_route(pds, "chat.bsky.group.rejectJoinRequest", 200, "{}");
    CHECK(indigo_session_submit_chat_join_reject("g1", "did:plc:dave"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_JOIN_REJECT_DONE, &ev));
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.rejectJoinRequest") == 2);
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.approveJoinRequest") == 2);
    CHECK(indigo_session_chat_join_requests()->count == 0);
    CHECK(e2e_pds_hits(pds, "chat.bsky.group.listJoinRequests") == 1);

    indigo_session_stop();
    e2e_pds_free(pds);
}

/* A postView in the shape getTimeline answers with. */
static const char *
post_json(const char *n, const char *text)
{
    static char buf[4][1024];
    static int k;
    char *b = buf[k++ & 3];

    snprintf(b, 1024,
             "{\"uri\":\"at://did:plc:abc/app.bsky.feed.post/%s\",\"cid\":\"bafy%s\","
             "\"author\":{\"did\":\"did:plc:abc\",\"handle\":\"ewancroft.uk\","
             "\"displayName\":\"Ewan\"},\"record\":{\"$type\":\"app.bsky.feed.post\","
             "\"text\":\"%s\",\"createdAt\":\"2026-10-01T10:00:00.000Z\"},"
             "\"indexedAt\":\"2026-10-01T10:00:00.000Z\"}",
             n, n, text);
    return b;
}

/* The timeline, one conversation's messages, the inbox and a reply, through
 * the same submits the screens use. The read jobs keep their last good page
 * when the next fetch fails. */
static void
test_timeline_and_chat(const char *data_dir)
{
    e2e_pds *pds = NULL;
    int port = 0;
    char svc[64];
    char session_path[512];
    char timeline[4096];
    indigo_session_event ev;

    CHECK(e2e_pds_start(&pds, &port));
    if (!pds) {
        return;
    }
    snprintf(svc, sizeof svc, "http://127.0.0.1:%d", port);
    snprintf(session_path, sizeof session_path, "%s/session-feed.dat", data_dir);
    unlink(session_path);

    e2e_pds_route(pds, "com.atproto.server.createSession", 200,
                  "{\"did\":\"did:plc:abc\",\"handle\":\"ewancroft.uk\","
                  "\"accessJwt\":\"a.b.c\",\"refreshJwt\":\"d.e.f\",\"active\":true}");
    e2e_pds_route(pds, "com.atproto.identity.resolveHandle", 200,
                  "{\"did\":\"did:plc:alice\"}");
    e2e_pds_route(pds, "app.bsky.actor.getPreferences", 200,
                  "{\"preferences\":[{\"$type\":"
                  "\"app.bsky.actor.defs#threadViewPref\",\"sort\":\"newest\"}]}");
    snprintf(timeline, sizeof timeline,
             "{\"feed\":[{\"post\":%s},{\"post\":%s}],\"cursor\":\"c1\"}",
             post_json("1", "hello timeline"), post_json("2", "second"));
    e2e_pds_route(pds, "app.bsky.feed.getTimeline", 200, timeline);
    e2e_pds_route(pds, "chat.bsky.convo.getMessages", 200,
                  "{\"messages\":["
                  "{\"id\":\"m2\",\"rev\":\"2\",\"text\":\"newer\","
                  "\"sender\":{\"did\":\"did:plc:bob\"},"
                  "\"sentAt\":\"2026-10-01T10:01:00.000Z\"},"
                  "{\"id\":\"m1\",\"rev\":\"1\",\"text\":\"older\","
                  "\"sender\":{\"did\":\"did:plc:abc\"},"
                  "\"sentAt\":\"2026-10-01T10:00:00.000Z\"}]}");
    e2e_pds_route(pds, "chat.bsky.convo.listConvos", 200,
                  "{\"convos\":["
                  "{\"id\":\"c1\",\"rev\":\"1\",\"unreadCount\":2,"
                  "\"lastMessage\":{\"text\":\"see you\"},"
                  "\"members\":[{\"did\":\"did:plc:abc\",\"handle\":\"ewancroft.uk\"},"
                  "{\"did\":\"did:plc:bob\",\"handle\":\"bob.test\","
                  "\"displayName\":\"bob\"}]},"
                  "{\"id\":\"g1\",\"rev\":\"1\",\"unreadCount\":0,"
                  "\"kind\":{\"$type\":\"chat.bsky.convo.defs#groupConvo\"},"
                  "\"members\":[{\"did\":\"did:plc:abc\",\"handle\":\"ewancroft.uk\"},"
                  "{\"did\":\"did:plc:bob\",\"handle\":\"bob.test\"},"
                  "{\"did\":\"did:plc:carol\",\"handle\":\"carol.test\"}]},"
                  "{\"id\":\"self\",\"rev\":\"1\",\"unreadCount\":0,"
                  "\"members\":[{\"did\":\"did:plc:abc\","
                  "\"handle\":\"ewancroft.uk\"}]}],\"cursor\":\"cc\"}");
    e2e_pds_route(pds, "chat.bsky.convo.sendMessage", 200,
                  "{\"id\":\"m3\",\"rev\":\"3\",\"text\":\"hello there\","
                  "\"sender\":{\"did\":\"did:plc:abc\"},"
                  "\"sentAt\":\"2026-10-01T10:02:00.000Z\"}");

    CHECK(indigo_session_start(session_path));
    CHECK(indigo_session_submit_login(svc, "ewancroft.test", "app-pass"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SIGNED_IN, &ev));

    /* The timeline parses both posts, reports the cursor and fetches the
     * preferences once. */
    CHECK(indigo_session_submit_timeline(NULL));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_TIMELINE_PAGE, &ev));
    CHECK(ev.page_count == 2);
    CHECK_STR(ev.cursor, "c1");
    unsigned count = 0;
    const indigo_post *page = indigo_session_page(&count);
    CHECK(count == 2);
    CHECK_STR(page[0].text, "hello timeline");
    CHECK_STR(page[0].handle, "ewancroft.uk");
    CHECK_STR(page[1].text, "second");
    CHECK(e2e_pds_hits(pds, "app.bsky.feed.getTimeline") == 1);
    CHECK(e2e_pds_hits(pds, "app.bsky.actor.getPreferences") == 1);

    /* getMessages answers newest first; the thread screen keeps oldest first
     * and selects the newest, with the viewer's own message marked. */
    CHECK(indigo_session_submit_chat_messages("c1"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_THREAD_PAGE, &ev));
    CHECK(ev.page_count == 2);
    const indigo_chat_thread *thread = indigo_session_chat_thread();
    CHECK_STR(thread->convo_id, "c1");
    CHECK(thread->count == 2);
    CHECK_STR(thread->items[0].text, "older");
    CHECK(thread->items[0].mine);
    CHECK_STR(thread->items[1].text, "newer");
    CHECK(!thread->items[1].mine);
    CHECK(thread->selected == 1);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getMessages") == 1);

    /* The inbox keeps a direct convo, marks a group by its kind, skips a convo
     * with only the viewer in it, and carries the unread total and cursor. */
    CHECK(indigo_session_submit_chat_inbox());
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_PAGE, &ev));
    CHECK(ev.page_count == 2);
    const indigo_chat_inbox *inbox = indigo_session_chat_inbox();
    CHECK(inbox->count == 2);
    CHECK(inbox->unread == 2);
    CHECK_STR(inbox->cursor, "cc");
    CHECK(inbox->has_more);
    CHECK_STR(inbox->items[0].id, "c1");
    CHECK_STR(inbox->items[0].name, "bob");
    CHECK_STR(inbox->items[0].handle, "@bob.test");
    CHECK_STR(inbox->items[0].preview, "see you");
    CHECK(inbox->items[0].unread == 2);
    CHECK(!inbox->items[0].group);
    CHECK_STR(inbox->items[1].id, "g1");
    CHECK(inbox->items[1].group);
    CHECK_STR(inbox->items[1].name, "Group");
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.listConvos") == 1);

    /* A reply is refused locally when it is only spaces, and sent as a
     * messageInput with the conversation id when it is real. */
    CHECK(!indigo_session_submit_chat_send("c1", "   "));
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.sendMessage") == 0);
    CHECK(indigo_session_submit_chat_send("c1", "hello there"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_SEND_DONE, &ev));
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.sendMessage") == 1);
    CHECK(e2e_pds_request_contains(pds, "chat.bsky.convo.sendMessage",
                                   "\"convoId\":\"c1\""));
    CHECK(e2e_pds_request_contains(pds, "chat.bsky.convo.sendMessage",
                                   "\"text\":\"hello there\""));
    CHECK(e2e_pds_request_contains(pds, "chat.bsky.convo.sendMessage",
                                   "chat.bsky.convo.defs#messageInput"));

    /* A failed refresh surfaces the failure and leaves the last page alone. */
    e2e_pds_route(pds, "app.bsky.feed.getTimeline", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_timeline(NULL));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_TIMELINE_FAILED, &ev));
    CHECK(ev.failure != WF_FAIL_NONE);
    CHECK(indigo_session_page(&count) != NULL && count == 2);
    CHECK(e2e_pds_hits(pds, "app.bsky.feed.getTimeline") == 2);
    CHECK(e2e_pds_hits(pds, "app.bsky.actor.getPreferences") == 1);

    e2e_pds_route(pds, "chat.bsky.convo.getMessages", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_chat_messages("c1"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_THREAD_FAILED, &ev));
    CHECK(ev.failure != WF_FAIL_NONE);
    CHECK(indigo_session_chat_thread()->count == 2);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getMessages") == 2);

    e2e_pds_route(pds, "chat.bsky.convo.listConvos", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_chat_inbox());
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_FAILED, &ev));
    CHECK(ev.failure != WF_FAIL_NONE);
    CHECK(indigo_session_chat_inbox()->count == 2);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.listConvos") == 2);

    e2e_pds_route(pds, "chat.bsky.convo.sendMessage", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_chat_send("c1", "try again"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_SEND_FAILED, &ev));
    CHECK(ev.failure != WF_FAIL_NONE);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.sendMessage") == 2);

    indigo_session_stop();
    e2e_pds_free(pds);
}

/* The request inbox and its two decisions: a further page appends, accepting
 * moves the row into the loaded inbox (carrying its unread count), declining
 * only removes it, and a failed decision leaves the row where it was. */
static void
test_chat_requests_and_decisions(const char *data_dir)
{
    e2e_pds *pds = NULL;
    int port = 0;
    char svc[64];
    char session_path[512];
    char long_id[INDIGO_CHAT_ID_MAX + 16];
    indigo_session_event ev;

    CHECK(e2e_pds_start(&pds, &port));
    if (!pds) {
        return;
    }
    snprintf(svc, sizeof svc, "http://127.0.0.1:%d", port);
    snprintf(session_path, sizeof session_path, "%s/session-requests.dat", data_dir);
    unlink(session_path);

    e2e_pds_route(pds, "com.atproto.server.createSession", 200,
                  "{\"did\":\"did:plc:abc\",\"handle\":\"ewancroft.uk\","
                  "\"accessJwt\":\"a.b.c\",\"refreshJwt\":\"d.e.f\",\"active\":true}");
    e2e_pds_route(pds, "chat.bsky.convo.listConvoRequests", 200,
                  "{\"requests\":["
                  "{\"id\":\"r1\",\"rev\":\"1\",\"unreadCount\":1,"
                  "\"lastMessage\":{\"text\":\"hi there\"},"
                  "\"members\":[{\"did\":\"did:plc:abc\",\"handle\":\"ewancroft.uk\"},"
                  "{\"did\":\"did:plc:bob\",\"handle\":\"bob.test\","
                  "\"displayName\":\"bob\"}]},"
                  "{\"id\":\"r2\",\"rev\":\"1\",\"unreadCount\":0,"
                  "\"lastMessage\":{\"text\":\"hello\"},"
                  "\"members\":[{\"did\":\"did:plc:abc\",\"handle\":\"ewancroft.uk\"},"
                  "{\"did\":\"did:plc:carol\",\"handle\":\"carol.test\"}]}],"
                  "\"cursor\":\"rq\"}");
    e2e_pds_route(pds, "chat.bsky.convo.listConvos", 200,
                  "{\"convos\":["
                  "{\"id\":\"c1\",\"rev\":\"1\",\"unreadCount\":0,"
                  "\"lastMessage\":{\"text\":\"old\"},"
                  "\"members\":[{\"did\":\"did:plc:abc\",\"handle\":\"ewancroft.uk\"},"
                  "{\"did\":\"did:plc:bob\",\"handle\":\"bob.test\","
                  "\"displayName\":\"bob\"}]}]}");
    e2e_pds_route(pds, "chat.bsky.convo.acceptConvo", 200, "{}");
    e2e_pds_route(pds, "chat.bsky.convo.leaveConvo", 200, "{}");

    CHECK(indigo_session_start(session_path));
    CHECK(indigo_session_submit_login(svc, "ewancroft.test", "app-pass"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SIGNED_IN, &ev));

    /* The first page names the other member, falls back to the handle when no
     * display name is set, and carries the cursor for the next one. */
    CHECK(indigo_session_submit_chat_requests());
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_REQUESTS_PAGE, &ev));
    CHECK(ev.page_count == 2);
    const indigo_chat_inbox *requests = indigo_session_chat_requests();
    CHECK(requests->count == 2);
    CHECK(requests->unread == 1);
    CHECK_STR(requests->cursor, "rq");
    CHECK(requests->has_more);
    CHECK_STR(requests->items[0].id, "r1");
    CHECK_STR(requests->items[0].name, "bob");
    CHECK_STR(requests->items[0].handle, "@bob.test");
    CHECK_STR(requests->items[0].preview, "hi there");
    CHECK(requests->items[0].unread == 1);
    CHECK_STR(requests->items[1].id, "r2");
    /* No display name, so the handle stands in for it. */
    CHECK_STR(requests->items[1].name, "carol.test");
    CHECK_STR(requests->items[1].handle, "@carol.test");
    CHECK(!requests->items[1].group);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.listConvoRequests") == 1);

    /* There is no first page to ask for by cursor, so an empty one is refused
     * before it reaches the worker. */
    CHECK(!indigo_session_submit_chat_requests_more(NULL));
    CHECK(!indigo_session_submit_chat_requests_more(""));
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.listConvoRequests") == 1);

    /* A further page appends rather than replacing, and its empty cursor stops
     * the paging. */
    e2e_pds_route(pds, "chat.bsky.convo.listConvoRequests", 200,
                  "{\"requests\":["
                  "{\"id\":\"r3\",\"rev\":\"1\",\"unreadCount\":0,"
                  "\"lastMessage\":{\"text\":\"hey\"},"
                  "\"members\":[{\"did\":\"did:plc:abc\",\"handle\":\"ewancroft.uk\"},"
                  "{\"did\":\"did:plc:dave\",\"handle\":\"dave.test\","
                  "\"displayName\":\"dave\"}]}]}");
    CHECK(indigo_session_submit_chat_requests_more("rq"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_REQUESTS_PAGE, &ev));
    CHECK(ev.page_count == 3);
    CHECK(requests->count == 3);
    CHECK(requests->unread == 1);
    CHECK_STR(requests->items[2].id, "r3");
    CHECK_STR(requests->cursor, "");
    CHECK(!requests->has_more);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.listConvoRequests") == 2);

    /* The inbox is loaded first, because accepting only adds the row when there
     * is already an inbox to add it to. */
    CHECK(indigo_session_submit_chat_inbox());
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_PAGE, &ev));
    const indigo_chat_inbox *inbox = indigo_session_chat_inbox();
    CHECK(inbox->count == 1);
    CHECK_STR(inbox->items[0].id, "c1");

    /* Accepting sends the convo id, drops the row from the requests and puts it
     * at the top of the inbox with its unread count. */
    CHECK(indigo_session_submit_chat_accept("r1"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_ACCEPT_DONE, &ev));
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.acceptConvo") == 1);
    CHECK(e2e_pds_request_contains(pds, "chat.bsky.convo.acceptConvo",
                                   "\"convoId\":\"r1\""));
    CHECK(requests->count == 2);
    CHECK_STR(requests->items[0].id, "r2");
    CHECK_STR(requests->items[1].id, "r3");
    CHECK(requests->unread == 0);
    CHECK(inbox->count == 2);
    CHECK_STR(inbox->items[0].id, "r1");
    CHECK_STR(inbox->items[1].id, "c1");
    CHECK(inbox->unread == 1);
    CHECK_STR(inbox->cursor, "");

    /* Declining removes the row and does not touch the inbox. */
    CHECK(indigo_session_submit_chat_decline("r2"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_DECLINE_DONE, &ev));
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.leaveConvo") == 1);
    CHECK(e2e_pds_request_contains(pds, "chat.bsky.convo.leaveConvo",
                                   "\"convoId\":\"r2\""));
    CHECK(requests->count == 1);
    CHECK_STR(requests->items[0].id, "r3");
    CHECK(inbox->count == 2);

    /* An empty, blank, spaced or over-long id is refused before any request:
     * a cut id would name another conversation. */
    memset(long_id, 'x', sizeof long_id - 1);
    long_id[sizeof long_id - 1] = '\0';
    CHECK(!indigo_session_submit_chat_accept(NULL));
    CHECK(!indigo_session_submit_chat_accept(""));
    CHECK(!indigo_session_submit_chat_accept("bad id"));
    CHECK(!indigo_session_submit_chat_accept(long_id));
    CHECK(!indigo_session_submit_chat_decline(""));
    CHECK(!indigo_session_submit_chat_decline("bad id"));
    CHECK(!indigo_session_submit_chat_decline(long_id));
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.acceptConvo") == 1);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.leaveConvo") == 1);

    /* A refused decision surfaces the failure and leaves the row in place, with
     * no retry and no change to the inbox. */
    e2e_pds_route(pds, "chat.bsky.convo.acceptConvo", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_chat_accept("r3"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_ACCEPT_FAILED, &ev));
    CHECK(ev.failure != WF_FAIL_NONE);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.acceptConvo") == 2);
    CHECK(requests->count == 1);
    CHECK_STR(requests->items[0].id, "r3");
    CHECK(inbox->count == 2);

    e2e_pds_route(pds, "chat.bsky.convo.leaveConvo", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_chat_decline("r3"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_DECLINE_FAILED, &ev));
    CHECK(ev.failure != WF_FAIL_NONE);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.leaveConvo") == 2);
    CHECK(requests->count == 1);

    /* A decision on a conversation that is not in the request list still goes
     * to the server and changes no loaded list. */
    e2e_pds_route(pds, "chat.bsky.convo.acceptConvo", 200, "{}");
    CHECK(indigo_session_submit_chat_accept("c1"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_ACCEPT_DONE, &ev));
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.acceptConvo") == 3);
    CHECK(requests->count == 1);
    CHECK(inbox->count == 2);
    CHECK_STR(inbox->items[0].id, "r1");

    indigo_session_stop();
    e2e_pds_free(pds);
}

/* Starting a direct conversation: the local refusals, a handle that does not
 * resolve, a lookup that fails, the account itself, and the convo id the
 * worker holds for the caller to open. */
static void
test_chat_start(const char *data_dir)
{
    e2e_pds *pds = NULL;
    int port = 0;
    char svc[64];
    char session_path[512];
    indigo_session_event ev;

    CHECK(e2e_pds_start(&pds, &port));
    if (!pds) {
        return;
    }
    snprintf(svc, sizeof svc, "http://127.0.0.1:%d", port);
    snprintf(session_path, sizeof session_path, "%s/session-start.dat", data_dir);
    unlink(session_path);

    e2e_pds_route(pds, "com.atproto.server.createSession", 200,
                  "{\"did\":\"did:plc:abc\",\"handle\":\"ewancroft.uk\","
                  "\"accessJwt\":\"a.b.c\",\"refreshJwt\":\"d.e.f\",\"active\":true}");
    e2e_pds_route(pds, "com.atproto.identity.resolveHandle", 200,
                  "{\"did\":\"did:plc:carol\"}");
    e2e_pds_route(pds, "chat.bsky.convo.getConvoForMembers", 200,
                  "{\"convo\":{\"id\":\"c9\",\"rev\":\"1\",\"members\":["
                  "{\"did\":\"did:plc:abc\",\"handle\":\"ewancroft.uk\"},"
                  "{\"did\":\"did:plc:bob\",\"handle\":\"bob.test\"}]}}");

    CHECK(indigo_session_start(session_path));
    CHECK(indigo_session_submit_login(svc, "ewancroft.test", "app-pass"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SIGNED_IN, &ev));

    /* An empty entry and one that is neither a handle nor a DID are refused
     * before a job is queued. */
    CHECK(!indigo_session_submit_chat_start(NULL));
    CHECK(!indigo_session_submit_chat_start(""));
    CHECK(!indigo_session_submit_chat_start("   "));
    CHECK(!indigo_session_submit_chat_start("not a handle"));
    CHECK(!indigo_session_submit_chat_start("@"));
    CHECK(e2e_pds_hits(pds, "com.atproto.identity.resolveHandle") == 0);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getConvoForMembers") == 0);
    CHECK_STR(indigo_session_chat_started_convo(), "");

    /* A handle the service does not know says so, and nothing is opened. */
    e2e_pds_route(pds, "com.atproto.identity.resolveHandle", 404,
                  "{\"error\":\"InvalidRequest\",\"message\":\"Unable to resolve handle\"}");
    CHECK(indigo_session_submit_chat_start("@alice.test"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_START_FAILED, &ev));
    CHECK(strstr(ev.message, "No account has that handle.") != NULL);
    CHECK(e2e_pds_hits(pds, "com.atproto.identity.resolveHandle") == 1);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getConvoForMembers") == 0);
    CHECK_STR(indigo_session_chat_started_convo(), "");

    /* The AppView answers a handle it cannot resolve with 400 InvalidRequest;
     * some hosts use 404. Both mean the account is missing. A 500 is a lookup
     * that failed, and a 200 with no DID is a reply that cannot be used: they
     * must not be shown as "no account", which would send the person looking
     * for a typo when the service is the problem. */
    e2e_pds_route(pds, "com.atproto.identity.resolveHandle", 400,
                  "{\"error\":\"InvalidRequest\",\"message\":\"Unable to resolve handle\"}");
    CHECK(indigo_session_submit_chat_start("alice.test"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_START_FAILED, &ev));
    CHECK(strstr(ev.message, "No account has that handle.") != NULL);
    CHECK(e2e_pds_hits(pds, "com.atproto.identity.resolveHandle") == 2);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getConvoForMembers") == 0);

    e2e_pds_route(pds, "com.atproto.identity.resolveHandle", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_chat_start("alice.test"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_START_FAILED, &ev));
    CHECK(strstr(ev.message, "Could not look up that handle.") != NULL);
    CHECK(e2e_pds_hits(pds, "com.atproto.identity.resolveHandle") == 3);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getConvoForMembers") == 0);

    e2e_pds_route(pds, "com.atproto.identity.resolveHandle", 200, "{}");
    CHECK(indigo_session_submit_chat_start("alice.test"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_START_FAILED, &ev));
    CHECK(strstr(ev.message, "Could not look up that handle.") != NULL);
    CHECK(e2e_pds_hits(pds, "com.atproto.identity.resolveHandle") == 4);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getConvoForMembers") == 0);

    /* The signed-in account is refused before the lookup, so a DID needs no
     * request at all. */
    CHECK(indigo_session_submit_chat_start("did:plc:abc"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_START_FAILED, &ev));
    CHECK(strstr(ev.message, "That is your own account.") != NULL);
    CHECK(e2e_pds_hits(pds, "com.atproto.identity.resolveHandle") == 4);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getConvoForMembers") == 0);

    /* A DID goes straight to getConvoForMembers, and the answer's convo id is
     * held for the caller to open. */
    CHECK(indigo_session_submit_chat_start("did:plc:bob"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_START_DONE, &ev));
    CHECK(e2e_pds_hits(pds, "com.atproto.identity.resolveHandle") == 4);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getConvoForMembers") == 1);
    CHECK_STR(indigo_session_chat_started_convo(), "c9");

    /* A handle is trimmed and stripped of its '@' before it is resolved, and
     * the resolved DID is what the conversation is opened with. */
    e2e_pds_route(pds, "com.atproto.identity.resolveHandle", 200,
                  "{\"did\":\"did:plc:carol\"}");
    e2e_pds_route(pds, "chat.bsky.convo.getConvoForMembers", 200,
                  "{\"convo\":{\"id\":\"c10\",\"rev\":\"1\",\"members\":["
                  "{\"did\":\"did:plc:abc\",\"handle\":\"ewancroft.uk\"},"
                  "{\"did\":\"did:plc:carol\",\"handle\":\"alice.test\"}]}}");
    CHECK(indigo_session_submit_chat_start(" @alice.test "));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_START_DONE, &ev));
    CHECK(e2e_pds_hits(pds, "com.atproto.identity.resolveHandle") == 5);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getConvoForMembers") == 2);
    CHECK_STR(indigo_session_chat_started_convo(), "c10");

    /* An answer without a conversation id is refused rather than reported as
     * open, and it clears the id held from the last success. */
    e2e_pds_route(pds, "chat.bsky.convo.getConvoForMembers", 200,
                  "{\"convo\":{\"rev\":\"1\",\"members\":[]}}");
    CHECK(indigo_session_submit_chat_start("did:plc:bob"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_START_FAILED, &ev));
    CHECK(strstr(ev.message, "The server did not return a conversation.") != NULL);
    CHECK_STR(indigo_session_chat_started_convo(), "");
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getConvoForMembers") == 3);

    /* A failed getConvoForMembers surfaces the failure and opens nothing. */
    e2e_pds_route(pds, "chat.bsky.convo.getConvoForMembers", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_chat_start("did:plc:bob"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_CHAT_START_FAILED, &ev));
    CHECK(ev.failure != WF_FAIL_NONE);
    CHECK(e2e_pds_hits(pds, "chat.bsky.convo.getConvoForMembers") == 4);
    CHECK_STR(indigo_session_chat_started_convo(), "");

    indigo_session_stop();
    e2e_pds_free(pds);
}

/* Actor search, post search and one person's posts. The last two share the
 * post array and its event; a fresh fetch replaces what is held, an empty
 * query is refused with a failure rather than a request, and a failed page
 * keeps the results that are already there. */
static void
test_search(const char *data_dir)
{
    e2e_pds *pds = NULL;
    int port = 0;
    char svc[64];
    char session_path[512];
    char body[4096];
    indigo_session_event ev;

    CHECK(e2e_pds_start(&pds, &port));
    if (!pds) {
        return;
    }
    snprintf(svc, sizeof svc, "http://127.0.0.1:%d", port);
    snprintf(session_path, sizeof session_path, "%s/session-search.dat", data_dir);
    unlink(session_path);

    e2e_pds_route(pds, "com.atproto.server.createSession", 200,
                  "{\"did\":\"did:plc:abc\",\"handle\":\"ewancroft.uk\","
                  "\"accessJwt\":\"a.b.c\",\"refreshJwt\":\"d.e.f\",\"active\":true}");
    /* An actor with no handle cannot be opened and is left out. */
    e2e_pds_route(pds, "app.bsky.actor.searchActors", 200,
                  "{\"actors\":["
                  "{\"did\":\"did:plc:bob\",\"handle\":\"bob.test\",\"displayName\":\"bob\"},"
                  "{\"did\":\"did:plc:nohandle\",\"displayName\":\"nobody\"},"
                  "{\"did\":\"did:plc:carol\",\"handle\":\"carol.test\"}],"
                  "\"cursor\":\"ac\"}");
    snprintf(body, sizeof body, "{\"posts\":[%s,%s],\"cursor\":\"pc\"}",
             post_json("s1", "first hit"), post_json("s2", "second hit"));
    e2e_pds_route(pds, "app.bsky.feed.searchPosts", 200, body);
    snprintf(body, sizeof body,
             "{\"feed\":[{\"post\":%s},{\"post\":%s}],\"cursor\":\"fc\"}",
             post_json("a1", "one of mine"), post_json("a2", "another of mine"));
    e2e_pds_route(pds, "app.bsky.feed.getAuthorFeed", 200, body);

    CHECK(indigo_session_start(session_path));
    CHECK(indigo_session_submit_login(svc, "ewancroft.test", "app-pass"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SIGNED_IN, &ev));

    /* The first actor page carries the cursor and skips the actor with no
     * handle; a display name is absent rather than copied from the handle. */
    CHECK(indigo_session_submit_search("bob", NULL));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SEARCH_PAGE, &ev));
    CHECK(ev.page_count == 2);
    CHECK_STR(ev.cursor, "ac");
    unsigned count = 0;
    const indigo_actor *actors = indigo_session_search_results(&count);
    CHECK(count == 2);
    CHECK_STR(actors[0].handle, "bob.test");
    CHECK_STR(actors[0].display_name, "bob");
    CHECK_STR(actors[0].did, "did:plc:bob");
    CHECK_STR(actors[1].handle, "carol.test");
    CHECK_STR(actors[1].display_name, "");
    CHECK(e2e_pds_hits(pds, "app.bsky.actor.searchActors") == 1);

    /* A cursor from that page asks for the next one: the rows append, the
     * cursor goes out in the query string, and a reply with no cursor ends the
     * paging. */
    e2e_pds_route(pds, "app.bsky.actor.searchActors", 200,
                  "{\"actors\":[{\"did\":\"did:plc:erin\",\"handle\":\"erin.test\"}]}");
    CHECK(indigo_session_submit_search("bob", "ac"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SEARCH_PAGE, &ev));
    CHECK(ev.page_count == 3);
    CHECK_STR(ev.cursor, "");
    actors = indigo_session_search_results(&count);
    CHECK(count == 3);
    CHECK_STR(actors[2].handle, "erin.test");
    CHECK(e2e_pds_hits(pds, "app.bsky.actor.searchActors") == 2);
    CHECK(e2e_pds_request_path_contains(pds, "app.bsky.actor.searchActors", "cursor=ac"));

    /* No query is refused before a request is made, and a NULL one is not even
     * taken as a job. */
    CHECK(!indigo_session_submit_search(NULL, NULL));
    CHECK(indigo_session_submit_search("", NULL));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SEARCH_FAILED, &ev));
    CHECK(ev.failure == WF_FAIL_OTHER);
    CHECK(e2e_pds_hits(pds, "app.bsky.actor.searchActors") == 2);

    /* A fresh search replaces the held results rather than appending to them. */
    e2e_pds_route(pds, "app.bsky.actor.searchActors", 200,
                  "{\"actors\":[{\"did\":\"did:plc:dan\",\"handle\":\"dan.test\"}]}");
    CHECK(indigo_session_submit_search("dan", NULL));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SEARCH_PAGE, &ev));
    CHECK(ev.page_count == 1);
    CHECK_STR(ev.cursor, "");
    actors = indigo_session_search_results(&count);
    CHECK(count == 1);
    CHECK_STR(actors[0].handle, "dan.test");
    CHECK(e2e_pds_hits(pds, "app.bsky.actor.searchActors") == 3);

    /* A failed page raises the failure and leaves the last page alone. */
    e2e_pds_route(pds, "app.bsky.actor.searchActors", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_search("dan", NULL));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SEARCH_FAILED, &ev));
    CHECK(ev.failure != WF_FAIL_NONE);
    actors = indigo_session_search_results(&count);
    CHECK(count == 1);
    CHECK(e2e_pds_hits(pds, "app.bsky.actor.searchActors") == 4);

    /* Post search lands in its own array, so the actor results stay valid. */
    CHECK(indigo_session_submit_post_search("hit", NULL));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_POST_SEARCH_PAGE, &ev));
    CHECK(ev.page_count == 2);
    CHECK_STR(ev.cursor, "pc");
    unsigned posts = 0;
    const indigo_post *hits = NULL;
    indigo_session_post_search_results(&hits, &posts);
    CHECK(posts == 2);
    CHECK_STR(hits[0].text, "first hit");
    CHECK_STR(hits[1].text, "second hit");
    CHECK(indigo_session_search_results(&count) != NULL && count == 1);
    CHECK(e2e_pds_hits(pds, "app.bsky.feed.searchPosts") == 1);

    /* Its own cursor appends a page the same way. */
    snprintf(body, sizeof body, "{\"posts\":[%s]}", post_json("s3", "third hit"));
    e2e_pds_route(pds, "app.bsky.feed.searchPosts", 200, body);
    CHECK(indigo_session_submit_post_search("hit", "pc"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_POST_SEARCH_PAGE, &ev));
    CHECK(ev.page_count == 3);
    CHECK_STR(ev.cursor, "");
    indigo_session_post_search_results(&hits, &posts);
    CHECK(posts == 3);
    CHECK_STR(hits[2].text, "third hit");
    CHECK(e2e_pds_hits(pds, "app.bsky.feed.searchPosts") == 2);
    CHECK(e2e_pds_request_path_contains(pds, "app.bsky.feed.searchPosts", "cursor=pc"));

    /* A failed post search keeps the last page too. */
    e2e_pds_route(pds, "app.bsky.feed.searchPosts", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_post_search("hit", NULL));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SEARCH_FAILED, &ev));
    CHECK(ev.failure != WF_FAIL_NONE);
    indigo_session_post_search_results(&hits, &posts);
    CHECK(posts == 3);
    CHECK(e2e_pds_hits(pds, "app.bsky.feed.searchPosts") == 3);

    /* One person's posts are the same result type and event, so they replace
     * the search hits in the one array. */
    CHECK(indigo_session_submit_author_feed("did:plc:bob", 0, NULL));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_POST_SEARCH_PAGE, &ev));
    CHECK(ev.page_count == 2);
    CHECK_STR(ev.cursor, "fc");
    indigo_session_post_search_results(&hits, &posts);
    CHECK(posts == 2);
    CHECK_STR(hits[0].text, "one of mine");
    CHECK(e2e_pds_hits(pds, "app.bsky.feed.getAuthorFeed") == 1);

    /* And their cursor appends to that same array. */
    snprintf(body, sizeof body, "{\"feed\":[{\"post\":%s}]}",
             post_json("a3", "third of mine"));
    e2e_pds_route(pds, "app.bsky.feed.getAuthorFeed", 200, body);
    CHECK(indigo_session_submit_author_feed("did:plc:bob", 0, "fc"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_POST_SEARCH_PAGE, &ev));
    CHECK(ev.page_count == 3);
    indigo_session_post_search_results(&hits, &posts);
    CHECK(posts == 3);
    CHECK_STR(hits[2].text, "third of mine");
    CHECK(e2e_pds_hits(pds, "app.bsky.feed.getAuthorFeed") == 2);
    CHECK(e2e_pds_request_path_contains(pds, "app.bsky.feed.getAuthorFeed", "cursor=fc"));

    CHECK(!indigo_session_submit_author_feed(NULL, 0, NULL));
    CHECK(!indigo_session_submit_author_feed("", 0, NULL));
    e2e_pds_route(pds, "app.bsky.feed.getAuthorFeed", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_author_feed("did:plc:bob", 0, NULL));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SEARCH_FAILED, &ev));
    CHECK(ev.failure != WF_FAIL_NONE);
    indigo_session_post_search_results(&hits, &posts);
    CHECK(posts == 3);
    CHECK(e2e_pds_hits(pds, "app.bsky.feed.getAuthorFeed") == 3);

    indigo_session_stop();
    e2e_pds_free(pds);
}

/* Curated lists, one list's members and the muted and blocked accounts. The
 * members and the moderation lists share the actor array the search uses, so
 * each fetch is checked against what the previous one left there. */
static void
test_lists_and_moderation(const char *data_dir)
{
    e2e_pds *pds = NULL;
    int port = 0;
    char svc[64];
    char session_path[512];
    indigo_session_event ev;

    CHECK(e2e_pds_start(&pds, &port));
    if (!pds) {
        return;
    }
    snprintf(svc, sizeof svc, "http://127.0.0.1:%d", port);
    snprintf(session_path, sizeof session_path, "%s/session-lists.dat", data_dir);
    unlink(session_path);

    e2e_pds_route(pds, "com.atproto.server.createSession", 200,
                  "{\"did\":\"did:plc:abc\",\"handle\":\"ewancroft.uk\","
                  "\"accessJwt\":\"a.b.c\",\"refreshJwt\":\"d.e.f\",\"active\":true}");
    /* A list with no URI cannot be opened and is left out. */
    e2e_pds_route(pds, "app.bsky.graph.getLists", 200,
                  "{\"lists\":["
                  "{\"uri\":\"at://did:plc:abc/app.bsky.graph.list/1\",\"name\":\"Friends\","
                  "\"description\":\"close friends\"},"
                  "{\"uri\":\"at://did:plc:abc/app.bsky.graph.list/2\",\"name\":\"News\"},"
                  "{\"name\":\"No URI\"}],\"cursor\":\"lc\"}");
    e2e_pds_route(pds, "app.bsky.graph.getList", 200,
                  "{\"list\":{\"uri\":\"at://did:plc:abc/app.bsky.graph.list/1\","
                  "\"name\":\"Friends\"},\"items\":["
                  "{\"uri\":\"at://did:plc:abc/app.bsky.graph.listitem/1\","
                  "\"subject\":{\"did\":\"did:plc:bob\",\"handle\":\"bob.test\","
                  "\"displayName\":\"bob\"}},"
                  "{\"uri\":\"at://did:plc:abc/app.bsky.graph.listitem/2\","
                  "\"subject\":{\"did\":\"did:plc:carol\",\"handle\":\"carol.test\"}}],"
                  "\"cursor\":\"mc\"}");
    e2e_pds_route(pds, "app.bsky.graph.getMutes", 200,
                  "{\"mutes\":["
                  "{\"did\":\"did:plc:bob\",\"handle\":\"bob.test\",\"displayName\":\"bob\"},"
                  "{\"did\":\"did:plc:carol\",\"handle\":\"carol.test\"}],"
                  "\"cursor\":\"muc\"}");
    e2e_pds_route(pds, "app.bsky.graph.getBlocks", 200,
                  "{\"blocks\":[{\"did\":\"did:plc:dave\",\"handle\":\"dave.test\","
                  "\"displayName\":\"dave\"}]}");

    CHECK(indigo_session_start(session_path));
    CHECK(indigo_session_submit_login(svc, "ewancroft.test", "app-pass"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SIGNED_IN, &ev));

    /* Two lists, in order, with the cursor for the next page. */
    CHECK(indigo_session_submit_lists(NULL));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_LISTS_PAGE, &ev));
    CHECK(ev.page_count == 2);
    CHECK_STR(ev.cursor, "lc");
    unsigned count = 0;
    const indigo_list *lists = NULL;
    indigo_session_lists_results(&lists, &count);
    CHECK(count == 2);
    CHECK_STR(lists[0].uri, "at://did:plc:abc/app.bsky.graph.list/1");
    CHECK_STR(lists[0].name, "Friends");
    CHECK_STR(lists[0].description, "close friends");
    CHECK_STR(lists[1].name, "News");
    CHECK_STR(lists[1].description, "");
    CHECK(e2e_pds_hits(pds, "app.bsky.graph.getLists") == 1);

    /* The cursor asks for a second page, which appends rather than replaces. */
    e2e_pds_route(pds, "app.bsky.graph.getLists", 200,
                  "{\"lists\":[{\"uri\":\"at://did:plc:abc/app.bsky.graph.list/3\","
                  "\"name\":\"Music\"}]}");
    CHECK(indigo_session_submit_lists("lc"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_LISTS_PAGE, &ev));
    CHECK(ev.page_count == 3);
    CHECK_STR(ev.cursor, "");
    indigo_session_lists_results(&lists, &count);
    CHECK(count == 3);
    CHECK_STR(lists[2].name, "Music");
    CHECK(e2e_pds_hits(pds, "app.bsky.graph.getLists") == 2);
    CHECK(e2e_pds_request_path_contains(pds, "app.bsky.graph.getLists", "cursor=lc"));

    /* A fresh fetch replaces the held lists. */
    e2e_pds_route(pds, "app.bsky.graph.getLists", 200,
                  "{\"lists\":[{\"uri\":\"at://did:plc:abc/app.bsky.graph.list/2\","
                  "\"name\":\"News\"}]}");
    CHECK(indigo_session_submit_lists(NULL));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_LISTS_PAGE, &ev));
    CHECK(ev.page_count == 1);
    indigo_session_lists_results(&lists, &count);
    CHECK(count == 1);
    CHECK_STR(lists[0].name, "News");
    CHECK_STR(ev.cursor, "");
    CHECK(e2e_pds_hits(pds, "app.bsky.graph.getLists") == 3);

    /* A failed fetch keeps the lists that are already there. */
    e2e_pds_route(pds, "app.bsky.graph.getLists", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_lists(NULL));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SEARCH_FAILED, &ev));
    CHECK(ev.failure != WF_FAIL_NONE);
    indigo_session_lists_results(&lists, &count);
    CHECK(count == 1);
    CHECK_STR(lists[0].name, "News");
    CHECK(e2e_pds_hits(pds, "app.bsky.graph.getLists") == 4);

    /* One list's members land in the actor array and carry the cursor. */
    CHECK(!indigo_session_submit_list_members(NULL, NULL));
    CHECK(!indigo_session_submit_list_members("", NULL));
    CHECK(indigo_session_submit_list_members("at://did:plc:abc/app.bsky.graph.list/1", NULL));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SEARCH_PAGE, &ev));
    CHECK(ev.page_count == 2);
    CHECK_STR(ev.cursor, "mc");
    const indigo_actor *actors = indigo_session_search_results(&count);
    CHECK(count == 2);
    CHECK_STR(actors[0].handle, "bob.test");
    CHECK_STR(actors[0].display_name, "bob");
    CHECK_STR(actors[1].handle, "carol.test");
    CHECK(e2e_pds_hits(pds, "app.bsky.graph.getList") == 1);

    /* A member page asked for by cursor appends too. */
    e2e_pds_route(pds, "app.bsky.graph.getList", 200,
                  "{\"list\":{\"uri\":\"at://did:plc:abc/app.bsky.graph.list/1\","
                  "\"name\":\"Friends\"},\"items\":[{"
                  "\"uri\":\"at://did:plc:abc/app.bsky.graph.listitem/3\","
                  "\"subject\":{\"did\":\"did:plc:dan\",\"handle\":\"dan.test\"}}]}");
    CHECK(indigo_session_submit_list_members("at://did:plc:abc/app.bsky.graph.list/1", "mc"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SEARCH_PAGE, &ev));
    CHECK(ev.page_count == 3);
    CHECK_STR(ev.cursor, "");
    actors = indigo_session_search_results(&count);
    CHECK(count == 3);
    CHECK_STR(actors[2].handle, "dan.test");
    CHECK(e2e_pds_hits(pds, "app.bsky.graph.getList") == 2);
    CHECK(e2e_pds_request_path_contains(pds, "app.bsky.graph.getList", "cursor=mc"));

    /* A fresh fetch of the members replaces the held ones. */
    e2e_pds_route(pds, "app.bsky.graph.getList", 200,
                  "{\"list\":{\"uri\":\"at://did:plc:abc/app.bsky.graph.list/1\","
                  "\"name\":\"Friends\"},\"items\":[{"
                  "\"uri\":\"at://did:plc:abc/app.bsky.graph.listitem/4\","
                  "\"subject\":{\"did\":\"did:plc:erin\",\"handle\":\"erin.test\"}}]}");
    CHECK(indigo_session_submit_list_members("at://did:plc:abc/app.bsky.graph.list/1", NULL));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SEARCH_PAGE, &ev));
    CHECK(ev.page_count == 1);
    actors = indigo_session_search_results(&count);
    CHECK(count == 1);
    CHECK_STR(actors[0].handle, "erin.test");
    CHECK(e2e_pds_hits(pds, "app.bsky.graph.getList") == 3);

    /* A failed member fetch keeps the last page. */
    e2e_pds_route(pds, "app.bsky.graph.getList", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_list_members("at://did:plc:abc/app.bsky.graph.list/1", NULL));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SEARCH_FAILED, &ev));
    CHECK(ev.failure != WF_FAIL_NONE);
    actors = indigo_session_search_results(&count);
    CHECK(count == 1);
    CHECK(e2e_pds_hits(pds, "app.bsky.graph.getList") == 4);

    /* The muted and blocked accounts come through the same rows and event as
     * every other list of people. */
    CHECK(indigo_session_submit_mutes(NULL));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SEARCH_PAGE, &ev));
    CHECK(ev.page_count == 2);
    CHECK_STR(ev.cursor, "muc");
    actors = indigo_session_search_results(&count);
    CHECK(count == 2);
    CHECK_STR(actors[0].handle, "bob.test");
    CHECK_STR(actors[1].handle, "carol.test");
    CHECK(e2e_pds_hits(pds, "app.bsky.graph.getMutes") == 1);

    /* Their cursor appends, and an account with no handle is left out. */
    e2e_pds_route(pds, "app.bsky.graph.getMutes", 200,
                  "{\"mutes\":[{\"did\":\"did:plc:erin\",\"handle\":\"erin.test\"},"
                  "{\"did\":\"did:plc:nohandle\"}]}");
    CHECK(indigo_session_submit_mutes("muc"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SEARCH_PAGE, &ev));
    CHECK(ev.page_count == 3);
    CHECK_STR(ev.cursor, "");
    actors = indigo_session_search_results(&count);
    CHECK(count == 3);
    CHECK_STR(actors[2].handle, "erin.test");
    CHECK(e2e_pds_hits(pds, "app.bsky.graph.getMutes") == 2);
    CHECK(e2e_pds_request_path_contains(pds, "app.bsky.graph.getMutes", "cursor=muc"));

    CHECK(indigo_session_submit_blocks(NULL));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SEARCH_PAGE, &ev));
    CHECK(ev.page_count == 1);
    actors = indigo_session_search_results(&count);
    CHECK(count == 1);
    CHECK_STR(actors[0].handle, "dave.test");
    CHECK_STR(actors[0].display_name, "dave");
    CHECK(e2e_pds_hits(pds, "app.bsky.graph.getBlocks") == 1);

    /* A failed list of mutes raises the failure and leaves the blocked row. */
    e2e_pds_route(pds, "app.bsky.graph.getMutes", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_mutes(NULL));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SEARCH_FAILED, &ev));
    CHECK(ev.failure != WF_FAIL_NONE);
    actors = indigo_session_search_results(&count);
    CHECK(count == 1);
    CHECK_STR(actors[0].handle, "dave.test");
    CHECK(e2e_pds_hits(pds, "app.bsky.graph.getMutes") == 3);

    indigo_session_stop();
    e2e_pds_free(pds);
}

/* The saved feeds picker and one custom feed's posts. The picker survives a
 * failed getFeedGenerators by naming feeds after their record keys, and the
 * feed page is filtered by the account's muted words, which the timeline's
 * preferences fetch carries. */
static void
test_feeds(const char *data_dir)
{
    e2e_pds *pds = NULL;
    int port = 0;
    char svc[64];
    char session_path[512];
    char body[4096];
    indigo_session_event ev;
    static const char prefs[] =
        "{\"preferences\":["
        "{\"$type\":\"app.bsky.actor.defs#savedFeedsPrefV2\",\"items\":["
        "{\"type\":\"feed\",\"value\":\"at://did:plc:abc/app.bsky.feed.generator/tech\","
        "\"pinned\":true},"
        "{\"type\":\"timeline\",\"value\":\"following\",\"pinned\":true},"
        "{\"type\":\"feed\",\"value\":\"at://did:plc:abc/app.bsky.feed.generator/news\"}]},"
        "{\"$type\":\"app.bsky.actor.defs#mutedWordsPref\",\"items\":["
        "{\"id\":\"m1\",\"value\":\"spoiler\",\"targets\":[\"content\"],"
        "\"actorTarget\":\"all\"}]}]}";

    CHECK(e2e_pds_start(&pds, &port));
    if (!pds) {
        return;
    }
    snprintf(svc, sizeof svc, "http://127.0.0.1:%d", port);
    snprintf(session_path, sizeof session_path, "%s/session-feeds.dat", data_dir);
    unlink(session_path);

    e2e_pds_route(pds, "com.atproto.server.createSession", 200,
                  "{\"did\":\"did:plc:abc\",\"handle\":\"ewancroft.uk\","
                  "\"accessJwt\":\"a.b.c\",\"refreshJwt\":\"d.e.f\",\"active\":true}");
    e2e_pds_route(pds, "app.bsky.actor.getPreferences", 200, prefs);
    e2e_pds_route(pds, "app.bsky.feed.getFeedGenerators", 200,
                  "{\"feeds\":["
                  "{\"uri\":\"at://did:plc:abc/app.bsky.feed.generator/tech\","
                  "\"displayName\":\"Tech talk\"},"
                  "{\"uri\":\"at://did:plc:abc/app.bsky.feed.generator/news\","
                  "\"displayName\":\"News\"}]}");
    snprintf(body, sizeof body,
             "{\"feed\":[{\"post\":%s},{\"post\":%s}],\"cursor\":\"fc\"}",
             post_json("f1", "hello from tech"), post_json("f2", "a spoiler"));
    e2e_pds_route(pds, "app.bsky.feed.getFeed", 200, body);

    CHECK(indigo_session_start(session_path));
    CHECK(indigo_session_submit_login(svc, "ewancroft.test", "app-pass"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SIGNED_IN, &ev));

    /* The picker holds the two saved feeds, not the pinned timeline, named by
     * the generators' display names. */
    CHECK(indigo_session_submit_feeds());
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_FEEDS_PAGE, &ev));
    CHECK(ev.page_count == 2);
    unsigned count = 0;
    const indigo_list *feeds = NULL;
    indigo_session_feeds_results(&feeds, &count);
    CHECK(count == 2);
    CHECK_STR(feeds[0].uri, "at://did:plc:abc/app.bsky.feed.generator/tech");
    CHECK_STR(feeds[0].name, "Tech talk");
    CHECK_STR(feeds[0].description, "Custom feed");
    CHECK_STR(feeds[1].name, "News");
    CHECK(e2e_pds_hits(pds, "app.bsky.actor.getPreferences") == 1);
    CHECK(e2e_pds_hits(pds, "app.bsky.feed.getFeedGenerators") == 1);

    /* A failed name lookup still leaves a usable picker: the record key stands
     * in for the display name. */
    e2e_pds_route(pds, "app.bsky.feed.getFeedGenerators", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_feeds());
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_FEEDS_PAGE, &ev));
    CHECK(ev.page_count == 2);
    indigo_session_feeds_results(&feeds, &count);
    CHECK(count == 2);
    CHECK_STR(feeds[0].name, "tech");
    CHECK_STR(feeds[1].name, "news");
    CHECK(e2e_pds_hits(pds, "app.bsky.feed.getFeedGenerators") == 2);

    /* A failed preferences fetch surfaces the failure and keeps the picker. */
    e2e_pds_route(pds, "app.bsky.actor.getPreferences", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_feeds());
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SEARCH_FAILED, &ev));
    CHECK(ev.failure != WF_FAIL_NONE);
    indigo_session_feeds_results(&feeds, &count);
    CHECK(count == 2);
    /* Every picker fetch reads the preferences (one per submit above), and the
     * failed name lookup still read them. */
    CHECK(e2e_pds_hits(pds, "app.bsky.actor.getPreferences") == 3);

    /* One feed's posts: the muted word removes the second one, and the
     * preferences are fetched once for the whole sign-in. */
    e2e_pds_route(pds, "app.bsky.actor.getPreferences", 200, prefs);
    CHECK(indigo_session_submit_feed("at://did:plc:abc/app.bsky.feed.generator/tech", NULL));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_TIMELINE_PAGE, &ev));
    CHECK(ev.page_count == 1);
    CHECK_STR(ev.cursor, "fc");
    const indigo_post *page = indigo_session_page(&count);
    CHECK(count == 1);
    CHECK_STR(page[0].text, "hello from tech");
    CHECK(e2e_pds_hits(pds, "app.bsky.feed.getFeed") == 1);
    CHECK(e2e_pds_hits(pds, "app.bsky.actor.getPreferences") == 4);

    snprintf(body, sizeof body, "{\"feed\":[{\"post\":%s}]}",
             post_json("f3", "another feed post"));
    e2e_pds_route(pds, "app.bsky.feed.getFeed", 200, body);
    CHECK(indigo_session_submit_feed("at://did:plc:abc/app.bsky.feed.generator/tech", NULL));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_TIMELINE_PAGE, &ev));
    CHECK(ev.page_count == 1);
    page = indigo_session_page(&count);
    CHECK_STR(page[0].text, "another feed post");
    /* The preferences were loaded with the first feed page, so no second
     * request. */
    CHECK(e2e_pds_hits(pds, "app.bsky.actor.getPreferences") == 4);
    CHECK(e2e_pds_hits(pds, "app.bsky.feed.getFeed") == 2);

    /* A failed feed page keeps the posts already held. */
    CHECK(!indigo_session_submit_feed(NULL, NULL));
    CHECK(!indigo_session_submit_feed("", NULL));
    e2e_pds_route(pds, "app.bsky.feed.getFeed", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_feed("at://did:plc:abc/app.bsky.feed.generator/tech", NULL));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_TIMELINE_FAILED, &ev));
    CHECK(ev.failure != WF_FAIL_NONE);
    CHECK(indigo_session_page(&count) != NULL && count == 1);
    CHECK(e2e_pds_hits(pds, "app.bsky.feed.getFeed") == 3);

    indigo_session_stop();
    e2e_pds_free(pds);
}

/* Notifications: the reason mapping, the unread marks, the texts and targets a
 * row opens, the seen mark (once, and not for an empty list) and the failure
 * that keeps the list. */
static void
test_notifications(const char *data_dir)
{
    e2e_pds *pds = NULL;
    int port = 0;
    char svc[64];
    char session_path[512];
    indigo_session_event ev;

    CHECK(e2e_pds_start(&pds, &port));
    if (!pds) {
        return;
    }
    snprintf(svc, sizeof svc, "http://127.0.0.1:%d", port);
    snprintf(session_path, sizeof session_path, "%s/session-notes.dat", data_dir);
    unlink(session_path);

    e2e_pds_route(pds, "com.atproto.server.createSession", 200,
                  "{\"did\":\"did:plc:abc\",\"handle\":\"ewancroft.uk\","
                  "\"accessJwt\":\"a.b.c\",\"refreshJwt\":\"d.e.f\",\"active\":true}");
    e2e_pds_route(pds, "app.bsky.notification.listNotifications", 200,
                  "{\"notifications\":["
                  "{\"uri\":\"at://did:plc:abc/app.bsky.feed.post/n1\",\"cid\":\"c1\","
                  "\"author\":{\"did\":\"did:plc:bob\",\"handle\":\"bob.test\","
                  "\"displayName\":\"bob\"},\"reason\":\"like\","
                  "\"reasonSubject\":\"at://did:plc:abc/app.bsky.feed.post/p1\","
                  "\"record\":{},\"isRead\":false,\"indexedAt\":\"2026-10-01T10:00:00.000Z\"},"
                  "{\"uri\":\"at://did:plc:abc/app.bsky.feed.post/n2\",\"cid\":\"c2\","
                  "\"author\":{\"did\":\"did:plc:carol\",\"handle\":\"carol.test\"},"
                  "\"reason\":\"follow\",\"record\":{},\"isRead\":true},"
                  "{\"uri\":\"at://did:plc:abc/app.bsky.feed.post/n3\",\"cid\":\"c3\","
                  "\"author\":{\"did\":\"did:plc:dave\",\"handle\":\"dave.test\"},"
                  "\"reason\":\"reply\",\"record\":{\"$type\":\"app.bsky.feed.post\","
                  "\"text\":\"nice post\",\"createdAt\":\"2026-10-01T10:00:00.000Z\"},"
                  "\"isRead\":false},"
                  "{\"uri\":\"at://did:plc:abc/app.bsky.feed.post/n4\",\"cid\":\"c4\","
                  "\"author\":{\"did\":\"did:plc:erin\",\"handle\":\"erin.test\"},"
                  "\"reason\":\"mention\",\"record\":{\"$type\":\"app.bsky.feed.post\","
                  "\"text\":\"hey @you\",\"createdAt\":\"2026-10-01T10:00:00.000Z\"},"
                  "\"isRead\":true},"
                  "{\"uri\":\"at://did:plc:abc/app.bsky.feed.post/n5\",\"cid\":\"c5\","
                  "\"author\":{\"did\":\"did:plc:frank\",\"handle\":\"frank.test\"},"
                  "\"reason\":\"repost\","
                  "\"reasonSubject\":\"at://did:plc:abc/app.bsky.feed.post/p2\","
                  "\"record\":{},\"isRead\":true},"
                  "{\"uri\":\"at://did:plc:abc/app.bsky.feed.post/n6\",\"cid\":\"c6\","
                  "\"author\":{\"did\":\"did:plc:gina\",\"handle\":\"gina.test\"},"
                  "\"reason\":\"something-new\",\"record\":{},\"isRead\":true}],"
                  "\"cursor\":\"notes-2\"}");
    e2e_pds_route(pds, "app.bsky.notification.updateSeen", 200, "{}");

    CHECK(indigo_session_start(session_path));
    CHECK(indigo_session_submit_login(svc, "ewancroft.test", "app-pass"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_SIGNED_IN, &ev));

    CHECK(indigo_session_submit_notifications());
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_NOTIFICATIONS_PAGE, &ev));
    CHECK(ev.page_count == 6);
    unsigned count = 0;
    const indigo_notification *notes = indigo_session_notifications(&count);
    CHECK(count == 6);
    CHECK(notes[0].kind == INDIGO_NOTE_LIKE);
    CHECK_STR(notes[0].name, "bob");
    CHECK_STR(notes[0].handle, "bob.test");
    CHECK(notes[0].unread);
    /* A like opens the post that was liked, not the notification's own uri. */
    CHECK_STR(notes[0].target_uri, "at://did:plc:abc/app.bsky.feed.post/p1");
    CHECK(notes[1].kind == INDIGO_NOTE_FOLLOW);
    CHECK_STR(notes[1].name, "carol.test");
    CHECK(notes[1].target_uri[0] == '\0');
    CHECK(!notes[1].unread);
    CHECK(notes[2].kind == INDIGO_NOTE_REPLY);
    CHECK_STR(notes[2].target_uri, "at://did:plc:abc/app.bsky.feed.post/n3");
    CHECK_STR(notes[2].text, "nice post");
    CHECK(notes[3].kind == INDIGO_NOTE_MENTION);
    CHECK_STR(notes[3].text, "hey @you");
    CHECK(notes[4].kind == INDIGO_NOTE_REPOST);
    CHECK_STR(notes[4].target_uri, "at://did:plc:abc/app.bsky.feed.post/p2");
    CHECK(notes[5].kind == INDIGO_NOTE_OTHER);
    CHECK(notes[5].text[0] == '\0');
    CHECK(e2e_pds_hits(pds, "app.bsky.notification.listNotifications") == 1);
    /* The service named a next page; the event carries it for the caller. */
    CHECK_STR(ev.cursor, "notes-2");

    /* The list was marked seen once, with the time in the body. */
    CHECK(e2e_pds_hits(pds, "app.bsky.notification.updateSeen") == 1);
    CHECK(e2e_pds_request_contains(pds, "app.bsky.notification.updateSeen",
                                   "\"seenAt\":\""));

    /* The next page is appended, sent the cursor it was given, and does not
     * mark anything seen: those rows are older than the first page. */
    e2e_pds_route(pds, "app.bsky.notification.listNotifications", 200,
                  "{\"notifications\":["
                  "{\"uri\":\"at://did:plc:abc/app.bsky.feed.post/n7\",\"cid\":\"c7\","
                  "\"author\":{\"did\":\"did:plc:sam\",\"handle\":\"sam.test\"},"
                  "\"reason\":\"reply\",\"record\":{\"$type\":\"app.bsky.feed.post\","
                  "\"text\":\"one more\",\"createdAt\":\"2026-10-01T09:00:00.000Z\"},"
                  "\"isRead\":true}],"
                  "\"cursor\":\"notes-3\"}");
    CHECK(indigo_session_submit_notifications_more("notes-2"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_NOTIFICATIONS_PAGE, &ev));
    CHECK(ev.page_count == 7);
    CHECK_STR(ev.cursor, "notes-3");
    CHECK(e2e_pds_request_path_contains(pds, "app.bsky.notification.listNotifications",
                                        "cursor=notes-2"));
    notes = indigo_session_notifications(&count);
    CHECK(count == 7);
    CHECK(notes[6].kind == INDIGO_NOTE_REPLY);
    CHECK_STR(notes[6].text, "one more");
    CHECK(e2e_pds_hits(pds, "app.bsky.notification.updateSeen") == 1);

    /* A page that adds nothing ends the paging rather than leaving a cursor
     * that would ask for the same rows again. */
    e2e_pds_route(pds, "app.bsky.notification.listNotifications", 200,
                  "{\"notifications\":[],\"cursor\":\"notes-3\"}");
    CHECK(indigo_session_submit_notifications_more("notes-3"));
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_NOTIFICATIONS_PAGE, &ev));
    CHECK(ev.page_count == 7);
    CHECK(ev.cursor[0] == '\0');
    indigo_session_notifications(&count);
    CHECK(count == 7);
    CHECK(e2e_pds_hits(pds, "app.bsky.notification.updateSeen") == 1);

    /* Nothing to page from, so no request is made. */
    CHECK(!indigo_session_submit_notifications_more(""));
    CHECK(!indigo_session_submit_notifications_more(NULL));

    /* A failed fetch surfaces the failure, keeps the list and is not marked
     * seen. */
    e2e_pds_route(pds, "app.bsky.notification.listNotifications", 500,
                  "{\"error\":\"InternalServerError\",\"message\":\"try later\"}");
    CHECK(indigo_session_submit_notifications());
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_NOTIFICATIONS_FAILED, &ev));
    CHECK(ev.failure != WF_FAIL_NONE);
    indigo_session_notifications(&count);
    CHECK(count == 7);
    CHECK(e2e_pds_hits(pds, "app.bsky.notification.listNotifications") == 4);
    CHECK(e2e_pds_hits(pds, "app.bsky.notification.updateSeen") == 1);

    /* An empty list is not a seen mark to send either. */
    e2e_pds_route(pds, "app.bsky.notification.listNotifications", 200,
                  "{\"notifications\":[]}");
    CHECK(indigo_session_submit_notifications());
    CHECK(wait_for_kind(INDIGO_SESSION_EVENT_NOTIFICATIONS_PAGE, &ev));
    CHECK(ev.page_count == 0);
    indigo_session_notifications(&count);
    CHECK(count == 0);
    CHECK(e2e_pds_hits(pds, "app.bsky.notification.updateSeen") == 1);

    indigo_session_stop();
    e2e_pds_free(pds);
}

int
main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <data-dir>\n", argv[0]);
        return 2;
    }

    indigo_log_init();
    test_group_create_and_join_link(argv[1]);
    test_group_settings_and_join_requests(argv[1]);
    test_timeline_and_chat(argv[1]);
    test_chat_requests_and_decisions(argv[1]);
    test_chat_start(argv[1]);
    test_search(argv[1]);
    test_lists_and_moderation(argv[1]);
    test_feeds(argv[1]);
    test_notifications(argv[1]);
    indigo_log_shutdown();

    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
