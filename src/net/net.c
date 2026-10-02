#include "net/net.h"
#include "util/log.h"

#include <3ds.h>

#include <stdbool.h>

static bool s_initialised;

bool
indigo_net_init(void)
{
    if (s_initialised) {
        return true;
    }

    Result result = socInit((u32 *) memalign(0x1000, 0x100000));
    if (R_FAILED(result)) {
        indigo_log_error("socInit failed: 0x%08lx", (unsigned long) result);
        return false;
    }

    s_initialised = true;
    indigo_log_info("network socket layer initialised");
    return true;
}

void
indigo_net_shutdown(void)
{
    if (!s_initialised) {
        return;
    }

    socExit();
    s_initialised = false;
    indigo_log_info("network socket layer shut down");
}
