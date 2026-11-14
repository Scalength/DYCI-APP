#ifndef ENROLLMENT_H
#define ENROLLMENT_H

#include <stddef.h>
#include "subjects.h"

#define ENROLLMENT_FIELD_MAX 64
#define CONTACT_MAX          32
#define DATE_MAX             32
#define ENROLLMENT_ERR_MAX   256

#define STATUS_ENROLLED     "ENROLLED"
#define STATUS_NOT_ENROLLED "NOT ENROLLED"
#define GENDER_MALE         "MALE"
#define GENDER_FEMALE       "FEMALE"
#define STATUS1_OLD         "OLD"
#define STATUS1_NEW         "NEW"
#define STATUS2_REGULAR     "REGULAR"
#define STATUS2_IRREGULAR   "IRREGULAR"
#define HIGHLIGHT_NONE      "NONE"
#define HIGHLIGHT_YELLOW    "YELLOW"
#define HIGHLIGHT_GREEN     "GREEN"

/* One subject row of the form: name + whether the student is enrolled. */
typedef struct {
    char name[SUBJECT_NAME_MAX];
    int enrolled; /* 1 = ENROLLED, 0 = NOT ENROLLED */
} SubjectChoice;

/* The full submission — one row of the sheet. */
typedef struct {
    char submitted_at[DATE_MAX];      /* single timestamp for the DATE column */
    char student_id[ENROLLMENT_FIELD_MAX]; /* free text, e.g. "2026-0000" */
    char status1[16];                 /* OLD / NEW — dropdown under Student ID */
    char status2[16];                 /* REGULAR / IRREGULAR — dropdown */
    char last_name[ENROLLMENT_FIELD_MAX];
    char first_name[ENROLLMENT_FIELD_MAX];
    char middle_initial[8];
    char gender[8];
    char course[16];                  /* "BSMaRE" or "BSMT" */
    char year_level[4];               /* "1" .. "3" */
    char section[ENROLLMENT_FIELD_MAX];
    char contact_number[CONTACT_MAX];
    char committee[ENROLLMENT_FIELD_MAX];
    char highlight[12]; /* NONE / YELLOW / GREEN — optional whole-row
                         * fill in the sheet (flag a student, e.g. an
                         * error to re-check) */
    SubjectChoice subjects[MAX_SUBJECTS];
    int subject_count;
} Enrollment;

/* Business rules. Returns 0 if valid, else -1 with a human-readable
 * reason in `err`. */
int enrollment_validate(const Enrollment *e, char *err, size_t err_len);

/* Composes "Last, First M.I." (middle initial optional). */
void enrollment_format_name(const Enrollment *e, char *out, size_t out_cap);

/* Serializes the submission for the Apps Script backend:
 * {"token":..(only if APP_TOKEN is set in config.h),"submitted_at":..,
 *  "student_id":..,"status1":"OLD|NEW","status2":"REGULAR|IRREGULAR",
 *  "name":..,"gender":..,"course":..,"year_level":..,
 *  "section":..,"contact_number":..,"committee":..,
 *  "highlight":"NONE|YELLOW|GREEN",
 *  "subjects":[{"name":"NAV 2","status":"ENROLLED"}, ...]}
 * `subjects` lists EVERY subject on the form with its ENROLLED /
 * NOT ENROLLED choice, so the sheet can show the same dropdown.
 * Returns a malloc'd string the caller must free(), or NULL on OOM. */
char *enrollment_to_json(const Enrollment *e);

#endif /* ENROLLMENT_H */
