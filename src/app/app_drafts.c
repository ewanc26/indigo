/* Saved composer drafts (distinct from the queue).
 *
 * Saving here never publishes and never queues: the draft is written to its
 * own versioned file. Resuming removes it from the list and puts it back in the
 * compose buffer, so posting or saving again replaces that one copy instead of
 * leaving a duplicate behind. */

#include "app/app_internal.h"
#include "store/drafts_store.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static void
set_draft_status(indigo_app *app, const char *message, bool is_error)
{
    indigo_copy_utf8(app->draft_status, sizeof app->draft_status, message);
    app->draft_status_is_error = is_error;
}

static bool
persist_drafts(indigo_app *app)
{
    if (indigo_drafts_store_save(INDIGO_DRAFTS_PATH, &app->drafts) != INDIGO_STORE_OK) {
        set_draft_status(app, "Could not save drafts to the SD card.", true);
        return false;
    }
    return true;
}

void
indigo_app_open_drafts(indigo_app *app)
{
    indigo_app_push_screen(app);
    app->screen = INDIGO_SCREEN_DRAFTS;
    if (app->draft_selected >= app->drafts.count) {
        app->draft_selected = app->drafts.count ? app->drafts.count - 1 : 0;
    }
    app->draft_delete_armed = false;
    app->draft_status[0] = '\0';
    app->draft_status_is_error = false;
}

void
indigo_app_save_compose_draft(indigo_app *app)
{
    indigo_compose *c = &app->compose;
    indigo_saved_draft draft;
    unsigned before = app->drafts.count;

    if (c->sending) {
        indigo_copy_utf8(c->status, sizeof c->status, "Wait for the post in progress.");
        c->status_is_error = true;
        return;
    }
    if (c->text[0] == '\0') {
        indigo_copy_utf8(c->status, sizeof c->status, "Write the post first.");
        c->status_is_error = true;
        return;
    }
    if (c->thread_count > 0 || c->image[0] != '\0') {
        indigo_copy_utf8(c->status, sizeof c->status,
                         "Drafts hold one text post for now. Remove the image or thread first.");
        c->status_is_error = true;
        return;
    }

    memset(&draft, 0, sizeof draft);
    indigo_copy_utf8(draft.text, sizeof draft.text, c->text);
    draft.saved_at = (long long) time(NULL);
    draft.mode = c->mode;
    draft.reply_gate = c->reply_gate;
    draft.has_target = c->has_target;
    if (c->has_target) {
        indigo_copy_utf8(draft.target_uri, sizeof draft.target_uri, c->target.uri);
        indigo_copy_utf8(draft.target_cid, sizeof draft.target_cid, c->target.cid);
        indigo_copy_utf8(draft.target_handle, sizeof draft.target_handle, c->target.handle);
        indigo_copy_utf8(draft.target_text, sizeof draft.target_text, c->target.text);
        indigo_copy_utf8(draft.root_uri, sizeof draft.root_uri, c->root_uri);
        indigo_copy_utf8(draft.root_cid, sizeof draft.root_cid, c->root_cid);
    }
    if (!indigo_saved_drafts_add(&app->drafts, &draft)) {
        indigo_copy_utf8(c->status, sizeof c->status,
                         app->drafts.count >= INDIGO_SAVED_DRAFTS_MAX
                             ? "Drafts are full. Delete one first."
                             : "That draft cannot be saved.");
        c->status_is_error = true;
        return;
    }
    if (!persist_drafts(app)) {
        indigo_saved_drafts_remove(&app->drafts, before);
        indigo_copy_utf8(c->status, sizeof c->status,
                         "Could not save drafts to the SD card.");
        c->status_is_error = true;
        return;
    }

    c->text[0] = '\0';
    c->thread_count = 0;
    indigo_compose_clear_image(c);
    indigo_copy_utf8(c->status, sizeof c->status,
                     "Saved as a draft. Find it under Drafts in the menu.");
    c->status_is_error = false;
}

static void
resume_draft(indigo_app *app, unsigned index)
{
    const indigo_saved_draft *d = indigo_saved_drafts_get(&app->drafts, index);
    indigo_compose *c = &app->compose;

    if (!d) {
        return;
    }
    if (c->text[0] != '\0' || c->sending) {
        set_draft_status(app,
                         "Post, save or clear the current draft before opening another.",
                         true);
        return;
    }

    c->mode = d->mode;
    c->reply_gate = d->reply_gate;
    c->has_target = d->has_target;
    memset(&c->target, 0, sizeof c->target);
    if (d->has_target) {
        indigo_copy_utf8(c->target.uri, sizeof c->target.uri, d->target_uri);
        indigo_copy_utf8(c->target.cid, sizeof c->target.cid, d->target_cid);
        indigo_copy_utf8(c->target.handle, sizeof c->target.handle, d->target_handle);
        indigo_copy_utf8(c->target.text, sizeof c->target.text, d->target_text);
        indigo_copy_utf8(c->root_uri, sizeof c->root_uri, d->root_uri);
        indigo_copy_utf8(c->root_cid, sizeof c->root_cid, d->root_cid);
    } else {
        c->root_uri[0] = '\0';
        c->root_cid[0] = '\0';
    }
    indigo_copy_utf8(c->text, sizeof c->text, d->text);
    c->thread_count = 0;
    memset(c->thread_texts, 0, sizeof c->thread_texts);
    indigo_compose_clear_image(c);
    c->sending = false;
    indigo_copy_utf8(c->status, sizeof c->status,
                     "Draft resumed. Saving or posting it removes it from Drafts.");
    c->status_is_error = false;

    indigo_saved_drafts_remove(&app->drafts, index);
    persist_drafts(app);
    if (app->draft_selected >= app->drafts.count) {
        app->draft_selected = app->drafts.count ? app->drafts.count - 1 : 0;
    }
    indigo_app_push_screen(app);
    app->screen = INDIGO_SCREEN_COMPOSE;
}

void
indigo_app_update_drafts(indigo_app *app, const indigo_input *input)
{
    indigo_saved_drafts *list = &app->drafts;

    if (input->back) {
        app->draft_delete_armed = false;
        indigo_app_go_back(app);
        return;
    }
    if (list->count == 0) {
        return;
    }
    if (app->draft_selected >= list->count) {
        app->draft_selected = list->count - 1;
    }
    if (input->up && app->draft_selected > 0) {
        app->draft_selected--;
        app->draft_delete_armed = false;
    }
    if (input->down && app->draft_selected + 1 < list->count) {
        app->draft_selected++;
        app->draft_delete_armed = false;
    }
    if (input->confirm) {
        resume_draft(app, app->draft_selected);
        return;
    }
    if (input->refresh) {
        if (!app->draft_delete_armed) {
            app->draft_delete_armed = true;
            set_draft_status(app, "Press Y again to delete this draft.", true);
        } else {
            indigo_saved_drafts_remove(list, app->draft_selected);
            app->draft_delete_armed = false;
            persist_drafts(app);
            if (app->draft_selected >= list->count) {
                app->draft_selected = list->count ? list->count - 1 : 0;
            }
            set_draft_status(app, "Draft deleted.", false);
        }
    }
    if (input->touch_pressed) {
        unsigned page = (app->draft_selected / INDIGO_TIMELINE_ROWS) * INDIGO_TIMELINE_ROWS;
        indigo_action a = indigo_layout_hit_app(app, input->touch_x, input->touch_y);

        if (a >= INDIGO_ACTION_ROW0 && a <= INDIGO_ACTION_ROW2) {
            unsigned idx = page + (unsigned) (a - INDIGO_ACTION_ROW0);
            if (idx < list->count) {
                app->draft_selected = idx;
                app->draft_delete_armed = false;
            }
        } else if (a == INDIGO_ACTION_OPEN) {
            resume_draft(app, app->draft_selected);
        } else if (a == INDIGO_ACTION_REFRESH) {
            if (!app->draft_delete_armed) {
                app->draft_delete_armed = true;
                set_draft_status(app, "Press Y again to delete this draft.", true);
            } else {
                indigo_saved_drafts_remove(list, app->draft_selected);
                app->draft_delete_armed = false;
                persist_drafts(app);
                if (app->draft_selected >= list->count) {
                    app->draft_selected = list->count ? list->count - 1 : 0;
                }
                set_draft_status(app, "Draft deleted.", false);
            }
        } else if (a == INDIGO_ACTION_BACK) {
            app->draft_delete_armed = false;
            indigo_app_go_back(app);
        }
    }
}
