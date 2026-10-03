#ifndef INDIGO_ERRORS_H
#define INDIGO_ERRORS_H

/* What went wrong, in terms a person can act on. Never carries secrets. */
typedef enum {
    INDIGO_FAIL_NONE = 0,
    INDIGO_FAIL_BAD_CREDENTIALS,
    INDIGO_FAIL_NETWORK,
    INDIGO_FAIL_TIMEOUT,
    INDIGO_FAIL_TLS,
    INDIGO_FAIL_RATE_LIMIT,
    INDIGO_FAIL_SERVER,
    INDIGO_FAIL_BAD_RESPONSE,
    INDIGO_FAIL_NOT_READY,
    INDIGO_FAIL_OTHER,
} indigo_failure;

const char *indigo_failure_message(indigo_failure f);

/* Stable short tag for the log file. */
const char *indigo_failure_tag(indigo_failure f);

/*
 * Classify an HTTP status from the server. 0 means the transport failed
 * before any response, which callers classify separately.
 */
indigo_failure indigo_failure_from_http(int http_status);

#endif
