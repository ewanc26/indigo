#ifndef INDIGO_CLOCK_H
#define INDIGO_CLOCK_H

/* The platform clock. Parsing and formatting timestamps is Wolfram's
 * (wolfram/time.h); only reading the clock is Indigo's. */

/* Seconds since the Unix epoch, or 0 if the platform clock is unusable. A long
 * long because the 3DS's long is 32 bits and an expiry far in the future would
 * not fit. */
long long indigo_time_now(void);

#endif
