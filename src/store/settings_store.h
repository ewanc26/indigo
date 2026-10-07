#ifndef INDIGO_SETTINGS_STORE_H
#define INDIGO_SETTINGS_STORE_H

#include "store/settings_codec.h"

/* Settings hold no credentials, so this store differs from the session store
 * in two ways: a damaged file is moved aside and reported, but the app carries
 * on with defaults either way, and nothing is wiped on the way out. */
indigo_store_status indigo_settings_store_save(const char *path,
                                               const indigo_settings *s);

/* Leaves *out holding indigo_settings_defaults() when the file is absent or
 * unreadable, so the caller never has to initialise it first. */
indigo_store_status indigo_settings_store_load(const char *path,
                                               indigo_settings *out);

#endif
