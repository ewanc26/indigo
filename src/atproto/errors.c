#include "atproto/errors.h"

const char *
indigo_failure_message(wf_failure_kind f)
{
    switch (f) {
    case WF_FAIL_NONE:
        return "";
    case WF_FAIL_BAD_CREDENTIALS:
        return "Wrong handle or app password.";
    case WF_FAIL_NETWORK:
        return "Could not reach the service. Check Wi-Fi and the service URL.";
    case WF_FAIL_TIMEOUT:
        return "The service took too long to answer. Try again.";
    case WF_FAIL_TLS:
        return "Secure connection failed. Check the service URL and the 3DS clock.";
    case WF_FAIL_RATE_LIMIT:
        return "Too many attempts. Wait a few minutes and try again.";
    case WF_FAIL_SERVER:
        return "The service had a problem. Try again later.";
    case WF_FAIL_BAD_RESPONSE:
        return "The service sent a reply Indigo could not read.";
    case WF_FAIL_NOT_READY:
        return "Networking is not available in this build.";
    case WF_FAIL_OTHER:
        break;
    }
    return "Sign-in failed.";
}
