#include "atproto/labels.h"
#include "atproto/session_internal.h"
#include "util/log.h"

#include <cJSON.h>
#include <string.h>

#define CONTENT_LABEL_PREF "app.bsky.actor.defs#contentLabelPref"
#define ADULT_CONTENT_PREF "app.bsky.actor.defs#adultContentPref"

/* The account's raw preferences array, or NULL when it could not be read. A
 * write must never be built from a failed read: putPreferences replaces the
 * whole array, so it would erase every other preference the account has. */
static cJSON *
read_prefs(void)
{
    char *json = NULL;
    cJSON *prefs = NULL;

    if (wf_agent_get_preferences(g_session.agent, &json) != WF_OK || !json) {
        free(json);
        return NULL;
    }
    prefs = cJSON_Parse(json);
    free(json);
    if (!cJSON_IsArray(prefs)) {
        cJSON_Delete(prefs);
        return NULL;
    }
    return prefs;
}

static bool
type_is(const cJSON *pref, const char *type)
{
    const cJSON *ty = cJSON_GetObjectItemCaseSensitive(pref, "$type");

    return cJSON_IsString(ty) && ty->valuestring && strcmp(ty->valuestring, type) == 0;
}

static bool
label_is(const cJSON *pref, const char *label)
{
    const cJSON *l = cJSON_GetObjectItemCaseSensitive(pref, "label");

    return cJSON_IsString(l) && l->valuestring && strcmp(l->valuestring, label) == 0;
}

static bool
visibility_ok(const char *v)
{
    return strcmp(v, "hide") == 0 || strcmp(v, "warn") == 0 || strcmp(v, "ignore") == 0;
}

static void
fail(indigo_session_event *ev, const char *message)
{
    snprintf(ev->message, sizeof ev->message, "%s", message);
    ev->failure = WF_FAIL_OTHER;
    indigo_session_publish_event(ev);
}

void
indigo_session_do_label_pref(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_LABEL_PREF_FAILED};
    const bool adult = strcmp(j->actor, "adult") == 0;
    cJSON *prefs;
    cJSON *pref;
    char *json;
    wf_response res = {0};
    wf_status status;

    INDIGO_SESSION_AGENT_GUARD(ev);
    if (adult ? !(strcmp(j->label_value, "on") == 0 || strcmp(j->label_value, "off") == 0)
              : !visibility_ok(j->label_value)) {
        fail(&ev, "That is not a content label setting.");
        return;
    }

    prefs = read_prefs();
    if (!prefs) {
        /* A write built from a failed read would replace every other preference. */
        fail(&ev, "Could not read your preferences, so nothing was changed.");
        return;
    }

    /* Replace only the entry for this label, or the adult switch. */
    for (int i = cJSON_GetArraySize(prefs) - 1; i >= 0; i--) {
        const cJSON *it = cJSON_GetArrayItem(prefs, i);
        if (adult ? type_is(it, ADULT_CONTENT_PREF)
                  : (type_is(it, CONTENT_LABEL_PREF) && label_is(it, j->actor))) {
            cJSON_DeleteItemFromArray(prefs, i);
        }
    }

    pref = cJSON_CreateObject();
    if (adult) {
        cJSON_AddStringToObject(pref, "$type", ADULT_CONTENT_PREF);
        cJSON_AddBoolToObject(pref, "enabled", strcmp(j->label_value, "on") == 0);
    } else {
        cJSON_AddStringToObject(pref, "$type", CONTENT_LABEL_PREF);
        cJSON_AddStringToObject(pref, "label", j->actor);
        cJSON_AddStringToObject(pref, "visibility", j->label_value);
    }
    cJSON_AddItemToArray(prefs, pref);

    json = cJSON_PrintUnformatted(prefs);
    cJSON_Delete(prefs);
    if (!json) {
        fail(&ev, "Could not prepare the setting. It is unchanged.");
        return;
    }
    status = wf_agent_put_preferences(g_session.agent, json, &res);
    free(json);
    wf_response_free(&res);
    if (status != WF_OK) {
        indigo_log_warn("session: label preference %s not saved (%d)", j->actor, (int) status);
        fail(&ev, "Could not save that setting. It is unchanged.");
        return;
    }

    /* Rebuild the context from the server's copy, so the menu shows what the
     * account now holds rather than what was asked for. */
    indigo_session_refresh_label_context();
    indigo_log_info("session: label preference %s set to %s", j->actor, j->label_value);
    ev.kind = INDIGO_SESSION_EVENT_LABEL_PREF_DONE;
    indigo_session_publish_event(&ev);
}

bool
indigo_session_submit_label_pref(const char *label, const char *visibility)
{
    indigo_job j = {.kind = JOB_LABEL_PREF};

    snprintf(j.actor, sizeof j.actor, "%s", label ? label : "");
    snprintf(j.label_value, sizeof j.label_value, "%s", visibility ? visibility : "");
    return indigo_session_enqueue(&j);
}

void
indigo_session_label_snapshot(indigo_label_snapshot *out)
{
    if (!out) {
        return;
    }
    LightLock_Lock(&g_session.lock);
    indigo_labels_snapshot(out);
    LightLock_Unlock(&g_session.lock);
}
