#ifndef INDIGO_STORE_STATUS_H
#define INDIGO_STORE_STATUS_H

/* Shared vocabulary for Indigo's on-SDMC files. A codec turns bytes into a
 * struct; a store moves that struct to and from the filesystem. Both report
 * failure with the same small set of reasons, so a caller can tell a missing
 * file from a damaged one without caring which half produced it. */

typedef enum {
    INDIGO_CODEC_OK = 0,
    INDIGO_CODEC_EMPTY,
    INDIGO_CODEC_BAD_VERSION,
    INDIGO_CODEC_CORRUPT,
    INDIGO_CODEC_INCOMPLETE,
    INDIGO_CODEC_TOO_BIG,
} indigo_codec_status;

typedef enum {
    INDIGO_STORE_OK = 0,
    INDIGO_STORE_MISSING,
    INDIGO_STORE_IO,
    INDIGO_STORE_UNREADABLE, /* present but corrupt; moved aside, not deleted */
} indigo_store_status;

#endif
