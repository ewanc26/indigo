#include "atproto/atproto.h"
#include "util/log.h"

#include <stdbool.h>

#ifdef WOLFRAM_3DS
#include <wolfram/platform.h>
#include <wolfram/syntax.h>
#endif

static bool s_initialised;
static bool s_available;

bool
indigo_atproto_init(void)
{
    /*
     * Application-facing boundary for Wolfram. Wolfram's 3DS platform layer
     * owns the libctru socket service, so Indigo never calls socInit().
     */
    s_initialised = true;

#ifdef WOLFRAM_3DS
    if (wf_platform_init() != WF_OK) {
        indigo_log_error("Wolfram platform initialisation failed");
        return false;
    }

    /* A pure call into the library proves it is linked and callable. */
    if (!wf_syntax_handle_is_valid("example.bsky.social")) {
        indigo_log_error("Wolfram handle validation rejected a valid handle");
        wf_platform_shutdown();
        return false;
    }

    s_available = true;
    indigo_log_info("Wolfram linked and initialised");
#else
    indigo_log_warn("built without Wolfram; protocol features unavailable");
#endif

    return true;
}

void
indigo_atproto_shutdown(void)
{
    if (!s_initialised) {
        return;
    }

#ifdef WOLFRAM_3DS
    if (s_available) {
        wf_platform_shutdown();
    }
#endif

    s_initialised = false;
    s_available = false;
    indigo_log_info("Bluesky layer shut down");
}

bool
indigo_atproto_available(void)
{
    return s_available;
}
