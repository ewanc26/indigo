#include "atproto/errors.h"

const char *
indigo_failure_message(indigo_failure f)
{
    switch (f) {
    case INDIGO_FAIL_NONE:
        return "";
    case INDIGO_FAIL_BAD_CREDENTIALS:
        return "Wrong handle or app password.";
    case INDIGO_FAIL_NETWORK:
        return "Could not reach the service. Check Wi-Fi and the service URL.";
    case INDIGO_FAIL_TIMEOUT:
        return "The service took too long to answer. Try again.";
    case INDIGO_FAIL_TLS:
        return "Secure connection failed. Check the service URL and the 3DS clock.";
    case INDIGO_FAIL_RATE_LIMIT:
        return "Too many attempts. Wait a few minutes and try again.";
    case INDIGO_FAIL_SERVER:
        return "The service had a problem. Try again later.";
    case INDIGO_FAIL_BAD_RESPONSE:
        return "The service sent a reply Indigo could not read.";
    case INDIGO_FAIL_NOT_READY:
        return "Networking is not available in this build.";
    case INDIGO_FAIL_OTHER:
        break;
    }
    return "Sign-in failed.";
}

const char *
indigo_failure_tag(indigo_failure f)
{
    switch (f) {
    case INDIGO_FAIL_NONE:
        return "none";
    case INDIGO_FAIL_BAD_CREDENTIALS:
        return "bad-credentials";
    case INDIGO_FAIL_NETWORK:
        return "network";
    case INDIGO_FAIL_TIMEOUT:
        return "timeout";
    case INDIGO_FAIL_TLS:
        return "tls";
    case INDIGO_FAIL_RATE_LIMIT:
        return "rate-limit";
    case INDIGO_FAIL_SERVER:
        return "server";
    case INDIGO_FAIL_BAD_RESPONSE:
        return "bad-response";
    case INDIGO_FAIL_NOT_READY:
        return "not-ready";
    case INDIGO_FAIL_OTHER:
        break;
    }
    return "other";
}

indigo_failure
indigo_failure_from_http(int http_status)
{
    if (http_status == 400 || http_status == 401 || http_status == 403) {
        return INDIGO_FAIL_BAD_CREDENTIALS;
    }
    if (http_status == 429) {
        return INDIGO_FAIL_RATE_LIMIT;
    }
    if (http_status == 408 || http_status == 504) {
        return INDIGO_FAIL_TIMEOUT;
    }
    if (http_status >= 500 && http_status <= 599) {
        return INDIGO_FAIL_SERVER;
    }
    return INDIGO_FAIL_OTHER;
}
