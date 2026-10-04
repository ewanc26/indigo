#include "app/signin.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

void
indigo_signin_init(indigo_signin *s)
{
    *s = (indigo_signin) {0};
    strcpy(s->service, INDIGO_DEFAULT_SERVICE);
    s->focus = INDIGO_FIELD_HANDLE;
}

static const char *
skip_space(const char *p)
{
    while (*p && isspace((unsigned char) *p)) {
        p++;
    }
    return p;
}

static size_t
trimmed_len(const char *p)
{
    size_t n = strlen(p);

    while (n > 0 && isspace((unsigned char) p[n - 1])) {
        n--;
    }
    return n;
}

static bool
is_loopback_host(const char *host, size_t len)
{
    static const char *const names[] = {"localhost", "127.0.0.1", "[::1]"};

    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        size_t n = strlen(names[i]);

        if (len >= n && strncmp(host, names[i], n) == 0 &&
            (len == n || host[n] == ':' || host[n] == '/')) {
            return true;
        }
    }
    return false;
}

indigo_input_status
indigo_normalise_service(const char *text, char *out, size_t cap)
{
    const char *p = skip_space(text);
    size_t len = trimmed_len(p);
    char tmp[INDIGO_SERVICE_MAX + 16];

    const char *scheme_end = strstr(p, "://");
    size_t floor_len = (scheme_end && (size_t) (scheme_end - p) < len)
                           ? (size_t) (scheme_end - p) + 3
                           : 0;

    while (len > floor_len && p[len - 1] == '/') {
        len--;
    }
    if (len == 0) {
        return INDIGO_INPUT_EMPTY;
    }
    if (len >= sizeof tmp - 9) {
        return INDIGO_INPUT_TOO_LONG;
    }
    for (size_t i = 0; i < len; i++) {
        if (isspace((unsigned char) p[i]) || iscntrl((unsigned char) p[i])) {
            return INDIGO_INPUT_BAD_CHARS;
        }
    }

    const char *host;
    size_t host_len;
    if (len >= 8 && strncasecmp(p, "https://", 8) == 0) {
        memcpy(tmp, "https://", 8);
        memcpy(tmp + 8, p + 8, len - 8);
        tmp[len] = '\0';
        host = p + 8;
        host_len = len - 8;
    } else if (len >= 7 && strncasecmp(p, "http://", 7) == 0) {
        host = p + 7;
        host_len = len - 7;
        if (!is_loopback_host(host, host_len)) {
            return INDIGO_INPUT_BAD_SCHEME;
        }
        memcpy(tmp, p, len);
        tmp[len] = '\0';
    } else if (strstr(p, "://") != NULL && strstr(p, "://") < p + len) {
        return INDIGO_INPUT_BAD_SCHEME;
    } else {
        memcpy(tmp, "https://", 8);
        memcpy(tmp + 8, p, len);
        tmp[8 + len] = '\0';
        host = p;
        host_len = len;
    }
    if (host_len == 0) {
        return INDIGO_INPUT_EMPTY;
    }

    size_t out_len = strlen(tmp);
    if (out_len >= cap) {
        return INDIGO_INPUT_TOO_LONG;
    }
    memcpy(out, tmp, out_len + 1);
    return INDIGO_INPUT_OK;
}

indigo_input_status
indigo_normalise_handle(const char *text, char *out, size_t cap)
{
    const char *p = skip_space(text);
    size_t len = trimmed_len(p);

    if (len > 0 && p[0] == '@') {
        p++;
        len--;
    }
    if (len == 0) {
        return INDIGO_INPUT_EMPTY;
    }
    for (size_t i = 0; i < len; i++) {
        if (isspace((unsigned char) p[i]) || iscntrl((unsigned char) p[i])) {
            return INDIGO_INPUT_BAD_CHARS;
        }
    }
    if (len >= cap) {
        return INDIGO_INPUT_TOO_LONG;
    }
    memcpy(out, p, len);
    out[len] = '\0';
    return INDIGO_INPUT_OK;
}

indigo_input_status
indigo_signin_set_field(indigo_signin *s, indigo_field f, const char *text)
{
    switch (f) {
    case INDIGO_FIELD_SERVICE:
        return indigo_normalise_service(text, s->service, sizeof s->service);
    case INDIGO_FIELD_HANDLE:
        return indigo_normalise_handle(text, s->handle, sizeof s->handle);
    case INDIGO_FIELD_PASSWORD: {
        size_t len = strlen(text);

        while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
            len--;
        }
        if (len == 0) {
            return INDIGO_INPUT_EMPTY;
        }
        if (len >= sizeof s->password) {
            return INDIGO_INPUT_TOO_LONG;
        }
        memcpy(s->password, text, len);
        s->password[len] = '\0';
        return INDIGO_INPUT_OK;
    }
    case INDIGO_FIELD_COUNT:
        break;
    }
    return INDIGO_INPUT_EMPTY;
}

const char *
indigo_signin_field_value(const indigo_signin *s, indigo_field f)
{
    switch (f) {
    case INDIGO_FIELD_SERVICE:
        return s->service;
    case INDIGO_FIELD_HANDLE:
        return s->handle;
    case INDIGO_FIELD_PASSWORD:
        return s->password;
    case INDIGO_FIELD_COUNT:
        break;
    }
    return "";
}

const char *
indigo_signin_field_label(indigo_field f)
{
    switch (f) {
    case INDIGO_FIELD_SERVICE:
        return "Service";
    case INDIGO_FIELD_HANDLE:
        return "Handle or email";
    case INDIGO_FIELD_PASSWORD:
        return "App password (optional)";
    case INDIGO_FIELD_COUNT:
        break;
    }
    return "";
}

bool
indigo_signin_ready(const indigo_signin *s)
{
    /* An empty password selects the browser-based OAuth flow. */
    return s->service[0] && s->handle[0];
}

void
indigo_signin_display(const indigo_signin *s, indigo_field f, char *out,
                      size_t cap)
{
    const char *v = indigo_signin_field_value(s, f);
    size_t n = 0;

    if (cap == 0) {
        return;
    }
    if (f == INDIGO_FIELD_PASSWORD) {
        size_t len = strlen(v);

        for (; n < len && n + 1 < cap && n < 24; n++) {
            out[n] = '*';
        }
        out[n] = '\0';
        return;
    }
    size_t len = strlen(v);

    if (len + 1 > cap) {
        len = cap - 1;
    }
    memcpy(out, v, len);
    out[len] = '\0';
}

const char *
indigo_input_status_message(indigo_input_status st)
{
    switch (st) {
    case INDIGO_INPUT_OK:
        return "";
    case INDIGO_INPUT_EMPTY:
        return "That field cannot be empty.";
    case INDIGO_INPUT_TOO_LONG:
        return "That entry is too long.";
    case INDIGO_INPUT_BAD_SCHEME:
        return "Service must use https:// (http only for localhost).";
    case INDIGO_INPUT_BAD_CHARS:
        return "That entry has spaces or control characters.";
    }
    return "";
}

int
indigo_signin_apply_autofill(indigo_signin *s, const char *text)
{
    static const struct {
        const char *key;
        indigo_field field;
    } keys[] = {{"service=", INDIGO_FIELD_SERVICE},
                {"handle=", INDIGO_FIELD_HANDLE},
                {"password=", INDIGO_FIELD_PASSWORD}};
    int applied = 0;
    const char *p = text;

    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t) (nl - p) : strlen(p);
        char line[INDIGO_SERVICE_MAX + 16];

        if (len < sizeof line) {
            memcpy(line, p, len);
            line[len] = '\0';
            for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
                size_t kl = strlen(keys[i].key);

                if (strncmp(line, keys[i].key, kl) == 0 &&
                    indigo_signin_set_field(s, keys[i].field, line + kl) == INDIGO_INPUT_OK) {
                    applied++;
                }
            }
        }
        p = nl ? nl + 1 : p + len;
    }
    return applied;
}
