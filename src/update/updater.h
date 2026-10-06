#ifndef INDIGO_UPDATER_H
#define INDIGO_UPDATER_H

/* What the update screen knows, as a state machine with no I/O, so every
 * transition can be tested on the host. The network and the card are the
 * worker's (update_worker.c) and the swap is main.c's; this only records what
 * they report and says what a press of the button should do next.
 *
 * Nothing is downloaded until the person has been shown the version and has
 * pressed Install, and a failure at any step leaves the running build as it
 * was: see docs/UPDATE.md. */

#include <stdbool.h>
#include <stddef.h>

#include "update/update.h"

#define INDIGO_UPDATER_MSG_MAX 160

typedef enum {
    INDIGO_UPDATER_UNAVAILABLE = 0, /* this build cannot update itself */
    INDIGO_UPDATER_IDLE,            /* not checked yet */
    INDIGO_UPDATER_CHECKING,
    INDIGO_UPDATER_UP_TO_DATE,
    INDIGO_UPDATER_AVAILABLE,       /* a newer release is offered, nothing downloaded */
    INDIGO_UPDATER_DOWNLOADING,     /* downloading, checking and staging */
    INDIGO_UPDATER_READY,           /* staged and verified; the swap is next */
    INDIGO_UPDATER_INSTALLED,       /* swapped; restart from the Homebrew Menu */
    INDIGO_UPDATER_FAILED,
} indigo_updater_state;

/* What a press of the screen's one button asks the platform glue to do. */
typedef enum {
    INDIGO_UPDATER_ACT_NONE = 0,
    INDIGO_UPDATER_ACT_CHECK,
    INDIGO_UPDATER_ACT_INSTALL,
} indigo_updater_action;

typedef struct {
    indigo_updater_state state;
    char current[INDIGO_UPDATE_VERSION_MAX]; /* this build's release, x.y.z, or "" */
    char latest[INDIGO_UPDATE_VERSION_MAX];  /* the offered release, x.y.z, or "" */
    unsigned long size;                      /* its size in bytes */
    char message[INDIGO_UPDATER_MSG_MAX];    /* why UNAVAILABLE or FAILED */
} indigo_updater;

/* `describe` is the build's git-describe stamp. `can_swap` is whether the build
 * was launched from a .3dsx on the SD card (indigo_update_paths_from). A build
 * that is past its tag, dirty, untagged or launched some other way is
 * UNAVAILABLE, with the reason written for a person. */
void indigo_updater_init(indigo_updater *u, const char *describe, bool can_swap);

/* The button's action in the current state; NONE while busy or unavailable. */
indigo_updater_action indigo_updater_action_for(const indigo_updater *u);
/* The button's label ("Check for updates", "Install 0.7.0"), or "" for none. */
const char *indigo_updater_button_label(const indigo_updater *u, char *buf, size_t cap);

/* Transitions. Each ignores a call that does not fit the current state, so a
 * late event from a worker cannot move the screen somewhere it should not be. */
void indigo_updater_begin_check(indigo_updater *u);
void indigo_updater_check_done(indigo_updater *u, const char *latest, unsigned long size,
                               bool is_update);
void indigo_updater_begin_install(indigo_updater *u);
void indigo_updater_staged(indigo_updater *u);
void indigo_updater_installed(indigo_updater *u);
void indigo_updater_fail(indigo_updater *u, const char *message);

#endif
