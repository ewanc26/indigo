#ifndef INDIGO_REPORT_H
#define INDIGO_REPORT_H

#include <stdbool.h>
#include <stddef.h>

#define INDIGO_REPORT_REASON_MAX 64

/* The standard com.atproto.moderation.defs reason types. Kept in one table so
 * the menu, the validation and the worker cannot disagree about them. */
unsigned indigo_report_reason_count(void);
const char *indigo_report_reason_type(unsigned index);
const char *indigo_report_reason_label(unsigned index);
bool indigo_report_reason_known(const char *reason_type);

/* A post's authority is the author's DID: at://did:.../collection/rkey.
 * False when the URI is not an at:// post URI or its authority is not a DID. */
bool indigo_report_author_did(const char *post_uri, char *out, size_t cap);

#endif /* INDIGO_REPORT_H */
