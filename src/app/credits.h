#ifndef INDIGO_CREDITS_H
#define INDIGO_CREDITS_H

/*
 * The repositories behind Indigo and the rest of the stack. The Credits screen
 * prints each address in full because a 3DS cannot open a browser; there is
 * nothing to press, only something to read and type elsewhere.
 *
 * The list lives apart from the layout so the host tests can check it without
 * building a display list.
 */

typedef struct {
    const char *name;
    const char *url;
} indigo_credit_repo;

unsigned indigo_credit_repo_count(void);
const indigo_credit_repo *indigo_credit_repo_at(unsigned index);

#endif
