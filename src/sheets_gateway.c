/*
 * sheets_gateway.c — frameworks/drivers layer.
 * Posts one JSON enrollment to a Google Apps Script Web App.
 *
 * Note: Apps Script /exec endpoints answer the first POST with a 302
 * redirect to a googleusercontent.com URL that carries the actual result.
 * CURLOPT_FOLLOWLOCATION handles that chain for us; the enrollment is
 * already recorded server-side at the moment of the first POST.
 */
#include "sheets_gateway.h"

#include "config.h" /* APP_VERSION */

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

void sheets_gateway_init(void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void sheets_gateway_cleanup(void)
{
    curl_global_cleanup();
}

/*
 * Portable TLS trust store.
 *
 * On a clean Windows PC there is no CA bundle at the path libcurl was
 * compiled with (e.g. /ucrt64/etc/ssl/certs/ca-bundle.crt), so every
 * HTTPS request fails with curl error 77 (CURLE_SSL_CACERT_BADFILE).
 * Fix: the packaging ships `ca-bundle.crt` next to the executable and
 * we point CURLOPT_CAINFO at it explicitly.
 *
 * Returns the path when a readable ca-bundle.crt exists next to the
 * exe; otherwise NULL and libcurl falls back to its default (correct
 * on Linux, where the system store is used).
 */
static const char *ca_bundle_path(char *buf, size_t cap)
{
#ifdef _WIN32
    if (GetModuleFileNameA(NULL, buf, (DWORD)cap) == 0)
        return NULL;

    char *slash = strrchr(buf, '\\');
    char *fslash = strrchr(buf, '/');
    if (fslash && (!slash || fslash > slash))
        slash = fslash;
    if (!slash)
        return NULL;
    slash[1] = '\0';

    if (strlen(buf) + sizeof("ca-bundle.crt") > cap)
        return NULL;
    strcat(buf, "ca-bundle.crt");

    FILE *fp = fopen(buf, "rb");
    if (!fp)
        return NULL;
    fclose(fp);
    return buf;
#else
    (void)buf;
    (void)cap;
    return NULL; /* Linux/macOS: system default store is correct */
#endif
}

typedef struct {
    char  *buf;
    size_t used;
    size_t cap;
} ResponseBuf;

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    ResponseBuf *r = userdata;
    size_t total = size * nmemb;

    if (r->buf && r->cap > 0) {
        size_t space = r->cap - r->used - 1; /* always keep room for NUL */
        size_t copy  = total < space ? total : space;
        if (copy > 0) {
            memcpy(r->buf + r->used, ptr, copy);
            r->used += copy;
        }
        r->buf[r->used] = '\0';
    }
    return total; /* claim full consumption so the transfer isn't aborted */
}

long sheets_gateway_submit(const Enrollment *e,
                           const char *endpoint,
                           char *response_body,
                           size_t response_size)
{
    if (response_body && response_size > 0)
        response_body[0] = '\0';

    char *json = enrollment_to_json(e);
    if (!json)
        return -1;

    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");

    CURL *curl = curl_easy_init();
    if (!curl) {
        curl_slist_free_all(hdrs);
        free(json);
        return -1;
    }

    ResponseBuf rb = { response_body, 0, response_size };

    /* Use the portable CA bundle when it is present (see above). The
     * buffer stays alive until curl_easy_perform returns. */
    char ca_path[600];
    if (ca_bundle_path(ca_path, sizeof ca_path))
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca_path);

    curl_easy_setopt(curl, CURLOPT_URL, endpoint);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "enrollment-app/" APP_VERSION);
    if (response_body && response_size > 0) {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &rb);
    }

    CURLcode rc = curl_easy_perform(curl);

    long http = 0;
    if (rc == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    else
        http = -(long)rc; /* transport failure encoded as negative */

    curl_easy_cleanup(curl);
    curl_slist_free_all(hdrs);
    free(json);
    return http;
}
