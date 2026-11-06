#include "app/report.h"

#include <string.h>

static const struct {
    const char *type;
    const char *label;
} REASONS[] = {
    {"com.atproto.moderation.defs#reasonSpam", "Spam"},
    {"com.atproto.moderation.defs#reasonViolation", "Community rule violation"},
    {"com.atproto.moderation.defs#reasonMisleading", "Misleading content"},
    {"com.atproto.moderation.defs#reasonSexual", "Unwanted sexual content"},
    {"com.atproto.moderation.defs#reasonRude", "Harassment or rudeness"},
    {"com.atproto.moderation.defs#reasonOther", "Other"},
};

unsigned
indigo_report_reason_count(void)
{
    return (unsigned) (sizeof REASONS / sizeof REASONS[0]);
}

const char *
indigo_report_reason_type(unsigned index)
{
    return index < indigo_report_reason_count() ? REASONS[index].type : NULL;
}

const char *
indigo_report_reason_label(unsigned index)
{
    return index < indigo_report_reason_count() ? REASONS[index].label : NULL;
}

bool
indigo_report_reason_known(const char *reason_type)
{
    if (!reason_type) {
        return false;
    }
    for (unsigned i = 0; i < indigo_report_reason_count(); i++) {
        if (strcmp(REASONS[i].type, reason_type) == 0) {
            return true;
        }
    }
    return false;
}

bool
indigo_report_author_did(const char *post_uri, char *out, size_t cap)
{
    const char *start;
    const char *end;
    size_t n;

    if (out && cap) {
        out[0] = '\0';
    }
    if (!post_uri || !out || !cap || strncmp(post_uri, "at://", 5) != 0) {
        return false;
    }
    start = post_uri + 5;
    end = strchr(start, '/');
    if (!end) {
        return false;
    }
    n = (size_t) (end - start);
    if (n <= 4 || strncmp(start, "did:", 4) != 0 || n >= cap) {
        return false;
    }
    memcpy(out, start, n);
    out[n] = '\0';
    return true;
}
