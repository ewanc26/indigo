#include "update/updater.h"

#include <stdio.h>
#include <string.h>

static void
copy(char *dst, size_t cap, const char *src)
{
    snprintf(dst, cap, "%s", src ? src : "");
}

void
indigo_updater_init(indigo_updater *u, const char *describe, bool can_swap)
{
    bool dev = true;

    memset(u, 0, sizeof *u);
    if (!can_swap) {
        u->state = INDIGO_UPDATER_UNAVAILABLE;
        copy(u->message, sizeof u->message,
             "Indigo was not started from a .3dsx on the SD card, so it cannot replace itself.");
        return;
    }
    if (!describe || !indigo_update_release_of(describe, u->current, sizeof u->current, &dev)) {
        u->current[0] = '\0';
        u->state = INDIGO_UPDATER_UNAVAILABLE;
        copy(u->message, sizeof u->message,
             "This build does not say which release it is, so I cannot tell what would be newer.");
        return;
    }
    if (dev) {
        u->state = INDIGO_UPDATER_UNAVAILABLE;
        copy(u->message, sizeof u->message,
             "This is a development build, past its last release. Updating would not be an update.");
        return;
    }
    u->state = INDIGO_UPDATER_IDLE;
}

indigo_updater_action
indigo_updater_action_for(const indigo_updater *u)
{
    switch (u->state) {
    case INDIGO_UPDATER_IDLE:
    case INDIGO_UPDATER_UP_TO_DATE:
    case INDIGO_UPDATER_FAILED:
        return INDIGO_UPDATER_ACT_CHECK;
    case INDIGO_UPDATER_AVAILABLE:
        return INDIGO_UPDATER_ACT_INSTALL;
    default:
        return INDIGO_UPDATER_ACT_NONE;
    }
}

const char *
indigo_updater_button_label(const indigo_updater *u, char *buf, size_t cap)
{
    switch (indigo_updater_action_for(u)) {
    case INDIGO_UPDATER_ACT_CHECK:
        snprintf(buf, cap, "%s", u->state == INDIGO_UPDATER_FAILED ? "Try again" : "Check for updates");
        break;
    case INDIGO_UPDATER_ACT_INSTALL:
        snprintf(buf, cap, "Install %s", u->latest);
        break;
    default:
        if (cap) {
            buf[0] = '\0';
        }
    }
    return buf;
}

void
indigo_updater_begin_check(indigo_updater *u)
{
    if (indigo_updater_action_for(u) != INDIGO_UPDATER_ACT_CHECK) {
        return;
    }
    u->state = INDIGO_UPDATER_CHECKING;
    u->latest[0] = '\0';
    u->message[0] = '\0';
    u->size = 0;
}

void
indigo_updater_check_done(indigo_updater *u, const char *latest, unsigned long size, bool is_update)
{
    if (u->state != INDIGO_UPDATER_CHECKING) {
        return;
    }
    if (!is_update) {
        u->state = INDIGO_UPDATER_UP_TO_DATE;
        copy(u->latest, sizeof u->latest, latest);
        return;
    }
    copy(u->latest, sizeof u->latest, latest);
    u->size = size;
    u->state = INDIGO_UPDATER_AVAILABLE;
}

void
indigo_updater_begin_install(indigo_updater *u)
{
    if (u->state != INDIGO_UPDATER_AVAILABLE) {
        return;
    }
    u->state = INDIGO_UPDATER_DOWNLOADING;
    u->message[0] = '\0';
}

void
indigo_updater_staged(indigo_updater *u)
{
    if (u->state == INDIGO_UPDATER_DOWNLOADING) {
        u->state = INDIGO_UPDATER_READY;
    }
}

void
indigo_updater_installed(indigo_updater *u)
{
    if (u->state == INDIGO_UPDATER_READY) {
        u->state = INDIGO_UPDATER_INSTALLED;
    }
}

void
indigo_updater_fail(indigo_updater *u, const char *message)
{
    if (u->state == INDIGO_UPDATER_UNAVAILABLE || u->state == INDIGO_UPDATER_INSTALLED) {
        return;
    }
    u->state = INDIGO_UPDATER_FAILED;
    copy(u->message, sizeof u->message, message);
}
