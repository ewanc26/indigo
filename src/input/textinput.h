#ifndef INDIGO_TEXTINPUT_H
#define INDIGO_TEXTINPUT_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    INDIGO_TEXT_OK = 0,
    INDIGO_TEXT_CANCELLED,
    INDIGO_TEXT_FAILED,
} indigo_text_result;

/*
 * The one place Indigo asks the person to type. Everything else sees a
 * string, so another source (a dev autofill file, a future paste path) never
 * touches the screens. Blocks until the system keyboard closes.
 */
indigo_text_result indigo_text_edit(const char *hint, const char *initial,
                                    bool secret, char *out, size_t cap);

#endif
