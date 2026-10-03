#include "input/textinput.h"

#include <3ds.h>
#include <string.h>

indigo_text_result
indigo_text_edit(const char *hint, const char *initial, bool secret, char *out,
                 size_t cap)
{
    SwkbdState swkbd;
    SwkbdButton button;

    if (cap < 2) {
        return INDIGO_TEXT_FAILED;
    }

    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, (int) cap - 1);
    swkbdSetHintText(&swkbd, hint);
    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    if (secret) {
        swkbdSetPasswordMode(&swkbd, SWKBD_PASSWORD_HIDE_DELAY);
    } else if (initial && initial[0]) {
        swkbdSetInitialText(&swkbd, initial);
    }

    out[0] = '\0';
    button = swkbdInputText(&swkbd, out, cap);
    if (button != SWKBD_BUTTON_CONFIRM) {
        memset(out, 0, cap);
        return swkbdGetResult(&swkbd) == SWKBD_D1_CLICK0 ? INDIGO_TEXT_CANCELLED
                                                          : INDIGO_TEXT_FAILED;
    }
    return INDIGO_TEXT_OK;
}
