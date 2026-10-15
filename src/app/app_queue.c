/* The Queued posts screen and the Save for later path (AGENTS.md section 15).
 *
 * Nothing here sends on its own. A queued post goes out only when the person
 * chooses Send on its row, and it goes through the same publish request as a
 * post written on the compose screen. The queue is changed only by the
 * person's choices, and a post that fails to send returns to the queue. */

#include "app/app.h"
#include "app/app_internal.h"
#include "app/social.h"
#include "store/queue_store.h"
#include "ui/layout.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static void
set_queue_status(indigo_app *app, const char *message, bool is_error)
{
    indigo_copy_utf8(app->queue_status, sizeof app->queue_status, message);
    app->queue_status_is_error = is_error;
}

void
indigo_app_persist_queue(indigo_app *app)
{
    if (indigo_queue_store_save(INDIGO_QUEUE_PATH, &app->queue) != INDIGO_STORE_OK) {
        set_queue_status(app, "Could not save the queue to the SD card.", true);
    }
}

void
indigo_app_open_queue(indigo_app *app)
{
    indigo_app_push_screen(app);
    app->screen = INDIGO_SCREEN_QUEUE;
    if (app->queue_selected >= app->queue.count) {
        app->queue_selected = app->queue.count ? app->queue.count - 1 : 0;
    }
    app->queue_discard_armed = false;
    app->queue_status[0] = '\0';
}

void
indigo_app_save_compose_later(indigo_app *app)
{
    indigo_compose *c = &app->compose;

    if (app->queue_send_pending || c->sending) {
        indigo_copy_utf8(c->status, sizeof c->status, "Wait for the post in progress.");
        c->status_is_error = true;
        return;
    }
    if (c->text[0] == '\0') {
        indigo_copy_utf8(c->status, sizeof c->status, "Write the post first.");
        c->status_is_error = true;
        return;
    }
    if (c->has_target || c->thread_count > 0 || c->image[0] != '\0') {
        indigo_copy_utf8(c->status, sizeof c->status,
                         "Only a plain post can be saved for later.");
        c->status_is_error = true;
        return;
    }
    if (!indigo_queue_add(&app->queue, c->text, (long long) time(NULL))) {
        indigo_copy_utf8(c->status, sizeof c->status,
                         app->queue.count >= INDIGO_QUEUE_MAX
                             ? "The queue is full. Send or discard a post first."
                             : "That post is too long to save for later.");
        c->status_is_error = true;
        return;
    }
    indigo_app_persist_queue(app);
    c->text[0] = '\0';
    c->status[0] = '\0';
    c->status_is_error = false;
    /* Back to where composing started, with the saved post named there. */
    indigo_app_go_back(app);
    indigo_copy_utf8(app->timeline.status, sizeof app->timeline.status,
                     "Saved for later. Find it under Queued posts in the menu.");
    app->timeline.status_is_error = false;
}

/* Put the post at `index` into the compose buffer and publish it. The compose
 * buffer must be empty: the queue never replaces a draft the person is writing. */
void
indigo_app_queue_send(indigo_app *app, unsigned index)
{
    indigo_compose *c = &app->compose;
    indigo_queued_post post;

    if (app->queue_send_pending || c->sending) {
        set_queue_status(app, "A queued post is already being sent.", true);
        return;
    }
    if (app->request != INDIGO_REQUEST_NONE) {
        set_queue_status(app, "Wait for the connection to finish.", true);
        return;
    }
    if (c->text[0] != '\0') {
        set_queue_status(app, "Clear the new post first, then send from the queue.", true);
        return;
    }
    if (!indigo_queue_take(&app->queue, index, &post)) {
        return;
    }
    c->mode = INDIGO_COMPOSE_POST;
    c->has_target = false;
    c->thread_count = 0;
    indigo_compose_clear_image(c);
    indigo_copy_utf8(c->text, sizeof c->text, post.text);
    c->status[0] = '\0';
    c->sending = true;
    app->queue_inflight = post;
    app->queue_send_pending = true;
    app->request = INDIGO_REQUEST_PUBLISH;
    set_queue_status(app, "Sending...", false);
}

void
indigo_app_update_queue(indigo_app *app, const indigo_input *input)
{
    indigo_post_queue *q = &app->queue;

    if (input->back) {
        app->queue_discard_armed = false;
        indigo_app_go_back(app);
        return;
    }
    if (q->count == 0) {
        return;
    }
    if (app->queue_selected >= q->count) {
        app->queue_selected = q->count - 1;
    }
    if (input->up && app->queue_selected > 0) {
        app->queue_selected--;
        app->queue_discard_armed = false;
    }
    if (input->down && app->queue_selected + 1 < q->count) {
        app->queue_selected++;
        app->queue_discard_armed = false;
    }
    if (input->confirm) {
        indigo_app_queue_send(app, app->queue_selected);
        return;
    }
    if (input->refresh) {
        /* Discard takes two presses on the same post, so a stray press cannot
         * throw away something saved. */
        if (!app->queue_discard_armed) {
            app->queue_discard_armed = true;
            set_queue_status(app, "Press Y again to discard this post.", true);
        } else {
            indigo_queue_discard(q, app->queue_selected);
            app->queue_discard_armed = false;
            indigo_app_persist_queue(app);
            set_queue_status(app, "Discarded.", false);
        }
    }
    if (input->touch_pressed) {
        unsigned page = (app->queue_selected / INDIGO_TIMELINE_ROWS) * INDIGO_TIMELINE_ROWS;
        indigo_action a = indigo_layout_hit_app(app, input->touch_x, input->touch_y);

        if (a >= INDIGO_ACTION_ROW0 && a <= INDIGO_ACTION_ROW2) {
            unsigned idx = page + (unsigned) (a - INDIGO_ACTION_ROW0);
            if (idx < q->count) {
                app->queue_selected = idx;
                app->queue_discard_armed = false;
            }
        } else if (a == INDIGO_ACTION_OPEN) {
            indigo_app_queue_send(app, app->queue_selected);
        } else if (a == INDIGO_ACTION_REFRESH) {
            /* The same two-press discard as Y. */
            if (!app->queue_discard_armed) {
                app->queue_discard_armed = true;
                set_queue_status(app, "Press Y again to discard this post.", true);
            } else {
                indigo_queue_discard(q, app->queue_selected);
                app->queue_discard_armed = false;
                indigo_app_persist_queue(app);
                set_queue_status(app, "Discarded.", false);
            }
        } else if (a == INDIGO_ACTION_BACK) {
            app->queue_discard_armed = false;
            indigo_app_go_back(app);
        }
    }
}

/* A publish that finished for a queued post. Returns false when the publish was
 * not a queued one, so the caller handles it as before. */
bool
indigo_app_queue_publish_done(indigo_app *app)
{
    indigo_compose *c = &app->compose;

    if (!app->queue_send_pending) {
        return false;
    }
    app->queue_send_pending = false;
    c->sending = false;
    c->text[0] = '\0';
    indigo_compose_clear_image(c);
    indigo_app_persist_queue(app);
    set_queue_status(app, "Sent from the queue.", false);
    return true;
}

/* A publish that failed for a queued post: the text goes back in the queue, so
 * nothing is lost, and the reason is shown. */
bool
indigo_app_queue_publish_failed(indigo_app *app, const char *message)
{
    indigo_compose *c = &app->compose;

    if (!app->queue_send_pending) {
        return false;
    }
    app->queue_send_pending = false;
    c->sending = false;
    c->text[0] = '\0';
    indigo_compose_clear_image(c);
    if (!indigo_queue_add(&app->queue, app->queue_inflight.text,
                          app->queue_inflight.queued_at)) {
        /* Cannot happen: the send freed a slot and nothing could add while it was
         * in flight. Still say so rather than drop the post silently. */
        set_queue_status(app, "The post could not be put back in the queue.", true);
        return true;
    }
    indigo_app_persist_queue(app);
    set_queue_status(app, message && message[0] ? message
                                                : "That post did not send. It is still queued.",
                     true);
    return true;
}
