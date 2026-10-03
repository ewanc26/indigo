#include "app/app.h"
#include "atproto/atproto.h"
#include "app/signin.h"
#include "atproto/session.h"
#include "input/input.h"
#include "input/textinput.h"
#include "ui/ui.h"
#include "util/log.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define DATA_DIR "sdmc:/3ds/indigo"
#define LOG_PATH DATA_DIR "/indigo.log"
#define SESSION_PATH DATA_DIR "/session.dat"
#define AUTOFILL_PATH DATA_DIR "/autofill.txt"

static void
handle_edit(indigo_app *app, indigo_field f)
{
    char text[INDIGO_SERVICE_MAX];
    const char *current = f == INDIGO_FIELD_PASSWORD ? NULL : indigo_signin_field_value(&app->signin, f);
    indigo_text_result r;

    indigo_log_info("keyboard opened for field: %s", indigo_signin_field_label(f));
    r = indigo_text_edit(indigo_signin_field_label(f), current, f == INDIGO_FIELD_PASSWORD,
                         text, f == INDIGO_FIELD_PASSWORD ? INDIGO_PASSWORD_MAX : sizeof text);
    indigo_log_info("keyboard closed: %s", r == INDIGO_TEXT_OK ? "entered" : "cancelled");
    if (r == INDIGO_TEXT_OK) {
        indigo_app_set_field(app, f, text);
    }
    memset(text, 0, sizeof text);
}

static void
handle_requests(indigo_app *app)
{
    indigo_field f;

    switch (indigo_app_take_request(app, &f)) {
    case INDIGO_REQUEST_EDIT_FIELD:
        handle_edit(app, f);
        break;
    case INDIGO_REQUEST_SIGN_IN:
        if (indigo_session_submit_login(app->signin.service, app->signin.handle,
                                        app->signin.password)) {
            indigo_app_begin_sign_in(app, "Signing in...");
        }
        break;
    case INDIGO_REQUEST_SIGN_OUT:
        if (indigo_session_submit_logout()) {
            indigo_app_begin_sign_in(app, "Signing out...");
        }
        break;
    case INDIGO_REQUEST_NONE:
        break;
    }
}

static void
handle_events(indigo_app *app)
{
    indigo_session_event ev;

    while (indigo_session_poll(&ev)) {
        switch (ev.kind) {
        case INDIGO_SESSION_EVENT_SIGNED_IN:
            indigo_app_sign_in_succeeded(app, ev.account);
            break;
        case INDIGO_SESSION_EVENT_SIGN_IN_FAILED:
            /* No message when there was simply nothing saved to resume. */
            indigo_app_sign_in_failed(app, indigo_failure_message(ev.failure));
            break;
        case INDIGO_SESSION_EVENT_SIGNED_OUT:
            indigo_app_signed_out(app, "Signed out.");
            break;
        case INDIGO_SESSION_EVENT_NONE:
            break;
        }
    }
}

#ifdef INDIGO_DEV_AUTOFILL
/* Emulator aid only: never compiled into a normal build. */
static void
dev_autofill(indigo_app *app)
{
    char buf[1024];
    FILE *f = fopen(AUTOFILL_PATH, "rb");
    size_t n;

    if (!f) {
        return;
    }
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    if (indigo_signin_apply_autofill(&app->signin, buf) > 0) {
        indigo_log_warn("dev autofill applied (INDIGO_DEV_AUTOFILL build)");
        indigo_app_submit(app);
    }
    memset(buf, 0, sizeof buf);
}
#endif

int
main(void)
{
    indigo_log_init();
    mkdir("sdmc:/3ds", 0777);
    mkdir(DATA_DIR, 0777);
    if (!indigo_log_open_file(LOG_PATH)) {
        indigo_log_warn("no log file; continuing with stderr only");
    }

    gfxInitDefault();
    romfsInit();

    if (!indigo_ui_init()) {
        indigo_log_error("could not initialise the 3DS renderer");
        romfsExit();
        gfxExit();
        indigo_log_shutdown();
        return 1;
    }

    indigo_atproto_init();
    indigo_session_start(SESSION_PATH);

    indigo_input input;
    indigo_input_init(&input);

    indigo_app app;
    indigo_app_init(&app);
    app.wolfram_linked = indigo_atproto_available();

    if (indigo_session_has_saved() && indigo_session_submit_resume()) {
        indigo_app_begin_sign_in(&app, "Resuming your session...");
    }
#ifdef INDIGO_DEV_AUTOFILL
    else {
        dev_autofill(&app);
    }
#endif

    while (aptMainLoop() && !indigo_app_should_quit(&app)) {
        hidScanInput();

        indigo_input_begin_frame(&input);
        indigo_input_poll(&input);

        indigo_app_update(&app, &input);
        handle_requests(&app);
        handle_events(&app);
        indigo_ui_draw(&app, &input);

        gspWaitForVBlank();
    }

    indigo_app_shutdown(&app);
    indigo_session_stop();
    indigo_input_shutdown(&input);
    indigo_atproto_shutdown();
    indigo_ui_shutdown();
    romfsExit();
    gfxExit();
    indigo_log_shutdown();

    return 0;
}
