#ifndef INDIGO_APP_INTERNAL_H
#define INDIGO_APP_INTERNAL_H

/*
 * What the app's files share and nothing else may use. The public interface is
 * app/app.h.
 *
 * app.c is the state machine's spine: init, screen history, the requests the
 * platform glue performs and the per-frame dispatch. app_posts.c handles input
 * on the screens that show posts (home, thread, profile, notifications);
 * app_screens.c on the rest (menu, search, compose, settings, update, image);
 * app_results.c is what the platform glue calls when a request finishes.
 */

#include "app/app.h"
#include "app/signin.h"
#include "input/input.h"
#include "ui/layout.h"

#include <stdio.h>
#include <string.h>

/* app.c */
void indigo_app_set_status(indigo_signin *s, const char *msg, bool is_error);
void indigo_app_push_screen(indigo_app *app);
void indigo_app_go_back(indigo_app *app);
void indigo_app_load_thread(indigo_app *app, const char *uri);
void indigo_app_refresh_timeline(indigo_app *app);
void indigo_app_open_thread(indigo_app *app, const char *uri);
void indigo_app_open_profile(indigo_app *app, const char *actor);
void indigo_app_open_notifications(indigo_app *app);
void indigo_app_open_menu(indigo_app *app);
void indigo_app_toggle_like(indigo_app *app);
void indigo_app_toggle_repost(indigo_app *app);

/* app_posts.c */
int indigo_app_drag_rows(const indigo_app *app, const indigo_input *input);
void indigo_app_update_home(indigo_app *app, const indigo_input *input);
void indigo_app_update_notifications(indigo_app *app, const indigo_input *input);
void indigo_app_update_profile(indigo_app *app, const indigo_input *input);
void indigo_app_update_thread(indigo_app *app, const indigo_input *input);

/* app_screens.c */
void indigo_app_begin_compose(indigo_app *app, indigo_compose_mode mode, const indigo_post *target);
void indigo_app_submit_search(indigo_app *app);
void indigo_app_update_compose(indigo_app *app, const indigo_input *input);
void indigo_app_update_image(indigo_app *app, const indigo_input *input);
void indigo_app_update_menu(indigo_app *app, const indigo_input *input);
void indigo_app_update_search(indigo_app *app, const indigo_input *input);
void indigo_app_update_settings(indigo_app *app, const indigo_input *input);
void indigo_app_update_update(indigo_app *app, const indigo_input *input);

#endif
