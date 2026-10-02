#include "atproto/atproto.h"
#include "util/log.h"

#include <stdbool.h>

static bool s_initialised;

bool
indigo_atproto_init(void)
{
    /*
     * Wolfram integration starts here. Keep the boundary platform-specific:
     * the application should not know whether Wolfram is using 3DS curl,
     * mbedTLS or another transport.
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
