#include "net/net.h"
#include "util/log.h"

#include <3ds.h>

#include <stdlib.h>

#define SOC_BUFFER_SIZE (1u << 20)

static bool s_initialised;
static void *s_soc_buffer;

bool
indigo_net_init(void)
{
    if (s_initialised) {
        return true;
    }

    s_soc_buffer = memalign(0x1000, SOC_BUFFER_SIZE);
    if (!s_soc_buffer) {
        indigo_log_error("could not allocate the SOC buffer");
        return false;
    }

    Result result = socInit(s_soc_buffer, SOC_BUFFER_SIZE);
    if (R_FAILED(result)) {
        indigo_log_error("socInit failed: 0x%08lx", (unsigned long) result);
        free(s_soc_buffer);
        s_soc_buffer = NULL;
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
        free(s_soc_buffer);
        s_soc_buffer = NULL;
        return;
    }

    socExit();
    free(s_soc_buffer);
    s_soc_buffer = NULL;
    s_initialised = false;
    indigo_log_info("network socket layer shut down");
}
