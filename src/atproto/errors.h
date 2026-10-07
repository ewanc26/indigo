#ifndef INDIGO_ERRORS_H
#define INDIGO_ERRORS_H

/* What went wrong, in terms a person can act on. The kinds and their stable log
 * tags are Wolfram's (wolfram/failure.h: wf_failure_kind, wf_failure_classify,
 * wf_failure_tag); what stays here is the wording, which is Indigo's and the
 * 3DS's. Never carries secrets. */

#include <wolfram/failure.h>

const char *indigo_failure_message(wf_failure_kind f);

#endif
