/*
 * A tiny mock PDS for Indigo's host end-to-end tests: one loopback listener,
 * a canned JSON answer per NSID, and the request count and last body for each
 * route. Wolfram's test mock answers by NSID but keeps only the most recent
 * request, which cannot show that createGroup carried DIDs rather than the
 * text that was typed -- the caller sees getConvoMembers last. This server
 * keeps the per-route state instead. It is test scaffolding only; nothing
 * here is linked into the application.
 */

#ifndef INDIGO_E2E_MOCK_PDS_H
#define INDIGO_E2E_MOCK_PDS_H

#include <stdbool.h>

typedef struct e2e_pds e2e_pds;

/* Start on an ephemeral 127.0.0.1 port. */
bool e2e_pds_start(e2e_pds **out, int *out_port);

/* Set the answer for `nsid`, replacing any previous one. */
void e2e_pds_route(e2e_pds *pds, const char *nsid, int status, const char *body);

/* How many times `nsid` has been requested. */
int e2e_pds_hits(e2e_pds *pds, const char *nsid);

/* Whether the last request to `nsid` carried `needle` in its body. */
bool e2e_pds_request_contains(e2e_pds *pds, const char *nsid, const char *needle);

/* Whether the last request to `nsid` carried `needle` in its path or query
 * string. GET parameters such as a page cursor arrive there, not in the body. */
bool e2e_pds_request_path_contains(e2e_pds *pds, const char *nsid, const char *needle);

/* Stop the listener and free the routes. Safe with NULL. */
void e2e_pds_free(e2e_pds *pds);

#endif /* INDIGO_E2E_MOCK_PDS_H */
