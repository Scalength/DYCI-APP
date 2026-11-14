#ifndef SHEETS_GATEWAY_H
#define SHEETS_GATEWAY_H

#include "enrollment.h"
#include <stddef.h>

#define GATEWAY_RESPONSE_MAX 4096

/*
 * Adapter boundary: this module is the ONLY place that knows how a
 * submission actually reaches Google Sheets (HTTPS POST to an Apps Script
 * Web App via libcurl). Swap it for a database or API backend later
 * without touching the UI or validation layers.
 */

/* Call once at program start (initializes libcurl), and before exit. */
void sheets_gateway_init(void);
void sheets_gateway_cleanup(void);

/* Submit one enrollment to the Apps Script Web App at `endpoint`.
 *
 * Returns:
 *   200            -> success (HTTP status of the final response)
 *   other positive -> an HTTP status code from the server
 *   negative       -> transport failure: -(CURLcode), e.g. -7 means
 *                     "couldn't connect". See curl.h for the list.
 *
 * `response_body` (optional) receives the first GATEWAY_RESPONSE_MAX bytes
 * of the server's reply, NUL-terminated.
 */
long sheets_gateway_submit(const Enrollment *e,
                           const char *endpoint,
                           char *response_body,
                           size_t response_size);

#endif /* SHEETS_GATEWAY_H */
