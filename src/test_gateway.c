/*
 * test_gateway.c — CLI smoke test for the Google Sheets pipeline (v2).
 * Verifies your Apps Script deployment end-to-end without the GUI:
 *
 *   ./test_gateway "Dela Cruz" "Juan" "M" "2026-0001" "MALE" "BSMT" "1" \
 *                  "A" "09171234567" "Committee 1" "NAV 2,SEAM 2" \
 *                  "OLD" "REGULAR"
 *
 * Argument order: Last First MI STUDENT-ID GENDER COURSE YEAR SECTION
 *                 CONTACT COMMITTEE ["ENROLLED,SUBJECTS" [STATUS1 STATUS2]]
 * The last optional arguments: subjects as a comma-separated list (they
 * are sent ENROLLED; pass "" for none), then STATUS 1 (OLD|NEW) and
 * STATUS 2 (REGULAR|IRREGULAR) — defaulted to OLD / REGULAR when omitted.
 */
#define _POSIX_C_SOURCE 200809L /* localtime_r, strtok_r under -std=c11 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "enrollment.h"
#include "sheets_gateway.h"

static void now_string(char *buf, size_t cap)
{
    time_t t = time(NULL);
    struct tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &t);
#else
    localtime_r(&t, &tm_now);
#endif
    strftime(buf, cap, "%Y-%m-%d %H:%M:%S", &tm_now);
}

int main(int argc, char **argv)
{
    if (argc < 11) {
        fprintf(stderr,
            "Usage: %s \"Last\" \"First\" \"MI\" \"2026-0000\" "
            "\"MALE|FEMALE\" \"BSMaRE|BSMT\" \"1|2|3\" \"Section\" "
            "\"Contact\" \"Committee\" [\"SUBJ A,SUBJ B\"] "
            "[\"OLD|NEW\" \"REGULAR|IRREGULAR\"]\n", argv[0]);
        return 2;
    }

    Enrollment e = {0};
    snprintf(e.submitted_at,    sizeof e.submitted_at,    "%s", "");
    now_string(e.submitted_at, sizeof e.submitted_at);
    snprintf(e.last_name,       sizeof e.last_name,       "%s", argv[1]);
    snprintf(e.first_name,      sizeof e.first_name,      "%s", argv[2]);
    snprintf(e.middle_initial,  sizeof e.middle_initial,  "%s", argv[3]);
    snprintf(e.student_id,      sizeof e.student_id,      "%s", argv[4]);
    snprintf(e.gender,          sizeof e.gender,          "%s", argv[5]);
    snprintf(e.course,          sizeof e.course,          "%s", argv[6]);
    snprintf(e.year_level,      sizeof e.year_level,      "%s", argv[7]);
    snprintf(e.section,         sizeof e.section,         "%s", argv[8]);
    snprintf(e.contact_number,  sizeof e.contact_number,  "%s", argv[9]);
    snprintf(e.committee,       sizeof e.committee,       "%s", argv[10]);
    snprintf(e.status1,         sizeof e.status1,         "%s",
             argc >= 13 ? argv[12] : STATUS1_OLD);
    snprintf(e.status2,         sizeof e.status2,         "%s",
             argc >= 14 ? argv[13] : STATUS2_REGULAR);

    if (argc >= 12 && argv[11][0] != '\0') {
        char subs[1024];
        snprintf(subs, sizeof subs, "%s", argv[11]);
        char *saveptr = NULL;
        for (char *tok = strtok_r(subs, ",", &saveptr); tok;
             tok = strtok_r(NULL, ",", &saveptr)) {
            if (e.subject_count >= MAX_SUBJECTS)
                break;
            SubjectChoice *sc = &e.subjects[e.subject_count++];
            subjects_normalize(sc->name, sizeof sc->name, tok);
            sc->enrolled = 1;
        }
    }

    char err[ENROLLMENT_ERR_MAX];
    if (enrollment_validate(&e, err, sizeof err) != 0) {
        fprintf(stderr, "Invalid enrollment: %s\n", err);
        return 1;
    }

    /* Same override chain as the GUI: ENROLLMENT_ENDPOINT (e.g. from
     * endpoint.txt content) wins over the value compiled into config.h. */
    const char *endpoint = getenv("ENROLLMENT_ENDPOINT");
    if (!endpoint || endpoint[0] == '\0')
        endpoint = SHEETS_ENDPOINT;

    if (endpoint[0] == '\0' ||
        strstr(endpoint, "PASTE_YOUR_APPS_SCRIPT_EXEC_URL_HERE") != NULL) {
        fprintf(stderr,
                "Endpoint not configured. Put your Apps Script /exec URL\n"
                "into src/config.h, or run with ENROLLMENT_ENDPOINT set.\n");
        return 1;
    }

    sheets_gateway_init();

    char body[GATEWAY_RESPONSE_MAX] = {0};
    long status = sheets_gateway_submit(&e, endpoint, body, sizeof body);

    if (status == 200) {
        printf("Success (HTTP 200). Server said: %s\n"
               "Check the %s-%s | %s tab — a new row should be there.\n",
               body[0] ? body : "(empty)", e.course, e.year_level, e.section);
    } else if (status < 0) {
        printf("Network error (curl code %ld). Nothing was sent.\n", status);
    } else {
        printf("Server replied HTTP %ld. Response: %s\n", status, body);
    }

    sheets_gateway_cleanup();
    return (status == 200) ? 0 : 1;
}
