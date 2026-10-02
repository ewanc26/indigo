#include "atproto/atproto.h"
#include "util/log.h"

#include <stdbool.h>

static bool s_initialised;

bool
indigo_atproto_init(void)
{
    /*
     * This is the application-facing boundary for Wolfram.
     *
     * The adapter must not own the 3DS socket service or another HTTP/TLS
     * stack. Wolfram already provides the 3DS platform transport layer.
     *
     * The scaffold does not create a session yet. Real Wolfram integration
     * comes after the native application shell is established.
     */
    s_initialised = true;
    indigo_log_info("AT Protocol layer ready");
    return true;
}

void
indigo_atproto_shutdown(void)
{
    if (!s_initialised) {
        return;
    }

    s_initialised = false;
    indigo_log_info("AT Protocol layer shut down");
}
