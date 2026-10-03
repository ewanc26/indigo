#ifndef INDIGO_APP_H
#define INDIGO_APP_H

#include <stdbool.h>
#include <stddef.h>

typedef struct indigo_input indigo_input;

#define INDIGO_SERVICE_MAX 256
#define INDIGO_HANDLE_MAX 256
#define INDIGO_PASSWORD_MAX 128
#define INDIGO_STATUS_MAX 192
#define INDIGO_DEFAULT_SERVICE "https://bsky.social"

typedef enum {
    INDIGO_SCREEN_SIGNIN = 0,
    INDIGO_SCREEN_HOME,
    INDIGO_SCREEN_PROFILE,
    INDIGO_SCREEN_SEARCH,
} indigo_screen;

typedef enum {
    INDIGO_FIELD_SERVICE = 0,
    INDIGO_FIELD_HANDLE,
    INDIGO_FIELD_PASSWORD,
    INDIGO_FIELD_COUNT,
} indigo_field;

typedef enum {
    INDIGO_PHASE_IDLE = 0,
    INDIGO_PHASE_BUSY,
} indigo_phase;

/* Side effects the app wants; the platform glue performs them. */
typedef enum {
    INDIGO_REQUEST_NONE = 0,
    INDIGO_REQUEST_EDIT_FIELD,
    INDIGO_REQUEST_SIGN_IN,
    INDIGO_REQUEST_SIGN_OUT,
} indigo_request_kind;

typedef struct {
    char service[INDIGO_SERVICE_MAX];
    char handle[INDIGO_HANDLE_MAX];
    char password[INDIGO_PASSWORD_MAX];
    indigo_field focus;
    indigo_phase phase;
    /* A progress or error line. Never contains credentials. */
    char status[INDIGO_STATUS_MAX];
    bool status_is_error;
    /* Handle of the signed-in account, shown on the home screen. */
    char account[INDIGO_HANDLE_MAX];
} indigo_signin;

typedef struct {
    indigo_screen screen;
    bool quit_requested;
    bool wolfram_linked;
    indigo_signin signin;
    indigo_request_kind request;
    indigo_field request_field;
} indigo_app;

void indigo_app_init(indigo_app *app);
void indigo_app_update(indigo_app *app, const indigo_input *input);
void indigo_app_shutdown(indigo_app *app);
bool indigo_app_should_quit(const indigo_app *app);

/* Returns the pending request once, then clears it. */
indigo_request_kind indigo_app_take_request(indigo_app *app,
                                            indigo_field *field);

/* Store text the person entered; a rejected entry becomes a status message. */
bool indigo_app_set_field(indigo_app *app, indigo_field f, const char *text);

/* Start sign-in as if the button were pressed (no-op if incomplete or busy). */
void indigo_app_submit(indigo_app *app);

/* Results fed back by the platform glue. */
void indigo_app_begin_sign_in(indigo_app *app, const char *status);
void indigo_app_sign_in_succeeded(indigo_app *app, const char *account);
void indigo_app_sign_in_failed(indigo_app *app, const char *message);
void indigo_app_signed_out(indigo_app *app, const char *message);

#endif
