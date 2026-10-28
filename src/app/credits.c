#include "app/credits.h"

#include <stddef.h>

/*
 * The stack in reading order: the two console clients, the SDK they share, the
 * Mac client and the Wii client that came before Indigo. The URLs are the
 * canonical repositories, not release or documentation pages.
 */
static const indigo_credit_repo REPOS[] = {
    {"Cobalt", "https://github.com/ewanc26/cobalt"},
    {"Indigo", "https://github.com/ewanc26/indigo"},
    {"Wolfram", "https://github.com/ewanc26/wolfram"},
    {"Platinum", "https://github.com/ewanc26/platinum"},
    {"Channel Blue", "https://github.com/ewanc26/channel-blue"},
};

unsigned
indigo_credit_repo_count(void)
{
    return (unsigned) (sizeof REPOS / sizeof REPOS[0]);
}

const indigo_credit_repo *
indigo_credit_repo_at(unsigned index)
{
    if (index >= indigo_credit_repo_count()) {
        return NULL;
    }
    return &REPOS[index];
}
