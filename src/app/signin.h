#ifndef INDIGO_SIGNIN_H
#define INDIGO_SIGNIN_H

#include "app/app.h"

typedef enum {
    INDIGO_INPUT_OK = 0,
    INDIGO_INPUT_EMPTY,
    INDIGO_INPUT_TOO_LONG,
    INDIGO_INPUT_BAD_SCHEME,
    INDIGO_INPUT_BAD_CHARS,
} indigo_input_status;

void indigo_signin_init(indigo_signin *s);

/*
 * Turn what the user typed into a canonical service URL: trims whitespace and
 * trailing slashes, defaults to https://, and refuses plain http except for
 * loopback hosts (so a local test PDS works but credentials never cross the
 * network unencrypted by accident).
 */
indigo_input_status indigo_normalise_service(const char *text, char *out,
                                             size_t cap);

/* Trims, strips a leading '@', and rejects embedded whitespace. */
indigo_input_status indigo_normalise_handle(const char *text, char *out,
                                            size_t cap);

indigo_input_status indigo_signin_set_field(indigo_signin *s, indigo_field f,
                                            const char *text);
const char *indigo_signin_field_value(const indigo_signin *s, indigo_field f);
const char *indigo_signin_field_label(indigo_field f);
bool indigo_signin_ready(const indigo_signin *s);

/* Display text for a field; the password is replaced by asterisks. */
void indigo_signin_display(const indigo_signin *s, indigo_field f, char *out,
                           size_t cap);

/*
 * Development aid for emulators with no keyboard: fill fields from
 * "service=", "handle=" and "password=" lines. Returns how many applied.
 */
int indigo_signin_apply_autofill(indigo_signin *s, const char *text);

const char *indigo_input_status_message(indigo_input_status st);

#endif
