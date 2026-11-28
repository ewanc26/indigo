#ifndef INDIGO_ATPROTO_LABELS_H
#define INDIGO_ATPROTO_LABELS_H

/*
 * Content labels, turned into what a post row does with them.
 *
 * The decision is Wolfram's (wolfram/moderation.h): its engine knows the
 * protocol's global label definitions, the adult-content gate, self-labels and
 * which labelers count. This keeps one moderation context for the signed-in
 * account and flattens a post's labels into an indigo_post_mod once, on the
 * session worker, so drawing reads a byte and a reason string.
 *
 * Until the account's preferences arrive the context is the safe default:
 * adult content off, Bluesky's moderation labeler subscribed. Same design as
 * Cobalt's src/atproto/labels.c (AGENTS.md §2).
 */

#include "app/timeline.h"

#include <cJSON.h>
#include <stdbool.h>
#include <stddef.h>

struct cJSON;
struct wf_actor_preferences;
struct wf_labeler_service_list;

/* A short name for a label value, for a row's cover: "Adult content" for porn,
 * "Graphic media" for graphic-media and gore, the value itself otherwise. */
const char *indigo_labels_reason(const char *value);

/* Whether `mod` covers any of the post. */
bool indigo_post_mod_covers(const indigo_post_mod *mod);

/* The signed-in account, which the engine needs as `user_did`: without it a
 * `!no-unauthenticated` label applies to everyone. NULL or "" is signed out. */
void indigo_labels_set_viewer(const char *did);

/* Reset to the safe default. Safe to call repeatedly. */
void indigo_labels_reset(void);

/* Rebuild from the account's saved preferences. NULL is the safe default.
 * False, keeping the previous context, when memory runs out. */
bool indigo_labels_from_prefs(const struct wf_actor_preferences *prefs);

bool indigo_labels_adult_enabled(void);

/* The effective visibility of a global label ("hide", "warn" or "ignore"), as
 * the engine applies it. Never NULL. Read it with the session lock held, as
 * the worker rebuilds the context under the same lock. */
const char *indigo_labels_visibility(const char *label);

/* The labels the settings menu sets, and what the account has for them. */
#define INDIGO_CONFIGURABLE_LABEL_COUNT 4
extern const char *const INDIGO_CONFIGURABLE_LABELS[INDIGO_CONFIGURABLE_LABEL_COUNT];

typedef struct {
    bool adult;
    /* "hide", "warn" or "ignore" per label in INDIGO_CONFIGURABLE_LABELS. */
    char visibility[INDIGO_CONFIGURABLE_LABEL_COUNT][8];
} indigo_label_snapshot;

/* Whether a profile's avatar is covered for its labels (see labels.c). Worker
 * only. `labels` is the profile's labels array, or NULL. */
bool indigo_labels_avatar_covered(const char *did, const cJSON *labels, char *reason, size_t reason_size);

/* Copy the context into `out`. Call with the session lock held. */
void indigo_labels_snapshot(indigo_label_snapshot *out);

/* The labelers the context counts, Bluesky's first; borrowed until the context
 * is rebuilt. Returns how many. */
#define INDIGO_LABELS_ACCEPT_MAX 20
int indigo_labels_labelers(const char **out, int max);

/* Add those labelers' own definitions (a detailed getServices). Bounded at
 * INDIGO_LABELS_DEFS_MAX; duplicates and malformed ones are skipped. A rebuilt
 * context drops them. Returns how many were added. */
#define INDIGO_LABELS_DEFS_MAX 160
int indigo_labels_add_services(const struct wf_labeler_service_list *list);

/*
 * Decide `out` for one post from its labels and its author's (either may be
 * NULL), as Wolfram's typed views keep them. `viewer_did` is the signed-in
 * account. Values the context cannot interpret, from a source it counts, are
 * listed in out->unsupported rather than dropped.
 */
void indigo_labels_apply(indigo_post_mod *out, const char *uri, const char *author_did,
                         const struct cJSON *labels, const struct cJSON *author_labels,
                         const char *viewer_did);

#endif
