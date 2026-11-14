/*
 * enrollment.c — entities + use-case validation layer.
 * Free of GTK and curl includes: reusable by GUI, CLI, and tests.
 */
#include "enrollment.h"

#include "config.h" /* APP_TOKEN (optional shared secret, may be "") */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Trim leading/trailing whitespace without mutating the input. */
static void trimmed(const char *s, const char **start, size_t *len)
{
    size_t n = strlen(s);
    const char *b = s;
    const char *t = s + n;

    while (b < t && (unsigned char)*b <= ' ')
        b++;
    while (t > b && (unsigned char)t[-1] <= ' ')
        t--;

    *start = b;
    *len = (size_t)(t - b);
}

static void trim_copy(const char *src, char *dst, size_t cap)
{
    const char *b;
    size_t n;
    trimmed(src, &b, &n);
    if (n >= cap)
        n = cap - 1;
    memcpy(dst, b, n);
    dst[n] = '\0';
}

int enrollment_validate(const Enrollment *e, char *err, size_t err_len)
{
    const char *b;
    size_t n;

    if (!e || !err || err_len == 0)
        return -1;

    if (e->submitted_at[0] == '\0') {
        snprintf(err, err_len, "Internal error: missing submission timestamp.");
        return -1;
    }

    trimmed(e->last_name, &b, &n);
    if (n == 0) {
        snprintf(err, err_len, "Enter the student's last name.");
        return -1;
    }

    trimmed(e->first_name, &b, &n);
    if (n == 0) {
        snprintf(err, err_len, "Enter the student's first name.");
        return -1;
    }

    if (e->middle_initial[0] != '\0') {
        size_t mi_len = strlen(e->middle_initial);
        int alphas = 0;
        for (const char *p = e->middle_initial; *p; p++)
            if (isalpha((unsigned char)*p))
                alphas++;
        if (alphas != 1 || mi_len > 3) {
            snprintf(err, err_len,
                     "Middle initial must be a single letter (or left blank).");
            return -1;
        }
    }

    /* Student ID: required, but free-form — no format is enforced
     * (e.g. 2026-0000, 2026-053043, or any office-specific scheme). */
    if (e->student_id[0] == '\0') {
        snprintf(err, err_len, "Student ID is required.");
        return -1;
    }

    /* STATUS 1 / STATUS 2: dropdowns on the form, right below Student ID. */
    if (strcmp(e->status1, STATUS1_OLD) != 0 &&
        strcmp(e->status1, STATUS1_NEW) != 0) {
        snprintf(err, err_len, "Pick STATUS 1 (OLD or NEW).");
        return -1;
    }
    if (strcmp(e->status2, STATUS2_REGULAR) != 0 &&
        strcmp(e->status2, STATUS2_IRREGULAR) != 0) {
        snprintf(err, err_len, "Pick STATUS 2 (REGULAR or IRREGULAR).");
        return -1;
    }

    if (strcmp(e->gender, GENDER_MALE) != 0 &&
        strcmp(e->gender, GENDER_FEMALE) != 0) {
        snprintf(err, err_len, "Pick a gender.");
        return -1;
    }
    if (strcmp(e->course, PROGRAM_NAMES[0]) != 0 &&
        strcmp(e->course, PROGRAM_NAMES[1]) != 0) {
        snprintf(err, err_len, "Pick a course.");
        return -1;
    }
    if (strcmp(e->year_level, "1") != 0 && strcmp(e->year_level, "2") != 0 &&
        strcmp(e->year_level, "3") != 0) {
        snprintf(err, err_len, "Pick a year level.");
        return -1;
    }

    trimmed(e->section, &b, &n);
    if (n == 0) {
        snprintf(err, err_len, "Enter the section.");
        return -1;
    }

    {
        size_t digits = 0;
        for (const char *p = e->contact_number; *p; p++) {
            unsigned char c = (unsigned char)*p;
            if (isdigit(c)) {
                digits++;
            } else if (c != '+' && c != '-' && c != '(' && c != ')' && c != ' ') {
                snprintf(err, err_len,
                         "Contact number can only contain digits, spaces and + - ( ).");
                return -1;
            }
        }
        if (digits < 7 || digits > 15) {
            snprintf(err, err_len, "Contact number needs 7 to 15 digits.");
            return -1;
        }
    }

    trimmed(e->committee, &b, &n);
    if (n == 0) {
        snprintf(err, err_len, "Enter the enrollment committee.");
        return -1;
    }

    if (e->subject_count < 0 || e->subject_count > MAX_SUBJECTS) {
        snprintf(err, err_len, "Subject list is corrupted.");
        return -1;
    }
    for (int i = 0; i < e->subject_count; i++) {
        trimmed(e->subjects[i].name, &b, &n);
        if (n == 0) {
            snprintf(err, err_len, "A subject row is empty — edit the subject list.");
            return -1;
        }
        if (e->subjects[i].enrolled != 0 && e->subjects[i].enrolled != 1) {
            snprintf(err, err_len, "Subject \"%s\" has an invalid status.",
                     e->subjects[i].name);
            return -1;
        }
    }

    err[0] = '\0';
    return 0;
}

void enrollment_format_name(const Enrollment *e, char *out, size_t out_cap)
{
    char last[ENROLLMENT_FIELD_MAX];
    char first[ENROLLMENT_FIELD_MAX];
    char mi[4] = "";

    trim_copy(e->last_name, last, sizeof last);
    trim_copy(e->first_name, first, sizeof first);

    for (const char *p = e->middle_initial; *p; p++) {
        if (isalpha((unsigned char)*p)) {
            mi[0] = (char)toupper((unsigned char)*p);
            mi[1] = '.';
            mi[2] = '\0';
            break;
        }
    }

    if (mi[0])
        snprintf(out, out_cap, "%s, %s %s", last, first, mi);
    else
        snprintf(out, out_cap, "%s, %s", last, first);
}

/* ---------------- JSON ---------------- */

static size_t escaped_len(const char *s)
{
    size_t n = 0;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"': case '\\': case '\b': case '\f':
        case '\n': case '\r': case '\t':
            n += 2;
            break;
        default:
            n += (c < 0x20) ? 6 : 1;
        }
    }
    return n;
}

static char *escape_copy(const char *s)
{
    size_t n = escaped_len(s);
    char *out = malloc(n + 1);
    if (!out)
        return NULL;

    char *p = out;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':  *p++ = '\\'; *p++ = '"';  break;
        case '\\': *p++ = '\\'; *p++ = '\\'; break;
        case '\b': *p++ = '\\'; *p++ = 'b';  break;
        case '\f': *p++ = '\\'; *p++ = 'f';  break;
        case '\n': *p++ = '\\'; *p++ = 'n';  break;
        case '\r': *p++ = '\\'; *p++ = 'r';  break;
        case '\t': *p++ = '\\'; *p++ = 't';  break;
        default:
            if (c < 0x20)
                p += sprintf(p, "\\u%04x", c);
            else
                *p++ = (char)c; /* UTF-8 passes through */
        }
    }
    *p = '\0';
    return out;
}

char *enrollment_to_json(const Enrollment *e)
{
    if (!e)
        return NULL;

    char name[256];
    enrollment_format_name(e, name, sizeof name);

    const char *fields[] = {
        e->submitted_at, e->student_id, e->status1, e->status2, name,
        e->gender, e->course, e->year_level, e->section, e->contact_number,
        e->committee, e->highlight
    };

    char *esc[12];
    memset(esc, 0, sizeof esc);
    char **subj = NULL;

    size_t need = 160; /* keys + syntax */
    for (int i = 0; i < 12; i++) {
        esc[i] = escape_copy(fields[i]);
        if (!esc[i])
            goto oom;
        need += strlen(esc[i]) + 24;
    }

    const char *token = APP_TOKEN;
    char *tok_esc = NULL;
    if (token[0] != '\0') {
        tok_esc = escape_copy(token);
        if (!tok_esc)
            goto oom;
        need += strlen(tok_esc) + 16;
    }

    subj = calloc(e->subject_count > 0 ? (size_t)e->subject_count : 1,
                  sizeof(char *));
    if (!subj)
        goto oom;
    for (int i = 0; i < e->subject_count; i++) {
        subj[i] = escape_copy(e->subjects[i].name);
        if (!subj[i])
            goto oom;
        need += strlen(subj[i]) + 56; /* {"name":"..","status":"NOT ENROLLED"}, */
    }

    char *out = malloc(need);
    if (!out)
        goto oom;

    char *p = out;
    p += sprintf(p, "{");
    if (tok_esc)
        p += sprintf(p, "\"token\":\"%s\",", tok_esc);
    p += sprintf(p,
        "\"submitted_at\":\"%s\",\"student_id\":\"%s\","
        "\"status1\":\"%s\",\"status2\":\"%s\","
        "\"name\":\"%s\",\"gender\":\"%s\",\"course\":\"%s\","
        "\"year_level\":\"%s\",\"section\":\"%s\",\"contact_number\":\"%s\","
        "\"committee\":\"%s\",\"highlight\":\"%s\",\"subjects\":[",
        esc[0], esc[1], esc[2], esc[3], esc[4], esc[5],
        esc[6], esc[7], esc[8], esc[9], esc[10], esc[11]);

    int first = 1;
    for (int i = 0; i < e->subject_count; i++) {
        if (!subj[i])
            continue;
        p += sprintf(p, "%s{\"name\":\"%s\",\"status\":\"%s\"}",
                     first ? "" : ",", subj[i],
                     e->subjects[i].enrolled ? STATUS_ENROLLED
                                             : STATUS_NOT_ENROLLED);
        first = 0;
    }

    strcpy(p, "]}");

    for (int i = 0; i < 11; i++)
        free(esc[i]);
    for (int i = 0; i < e->subject_count; i++)
        free(subj[i]);
    free(subj);
    free(tok_esc);
    return out;

oom:
    free(tok_esc);
    for (int i = 0; i < 11; i++)
        free(esc[i]);
    if (subj) {
        for (int i = 0; i < e->subject_count; i++)
            free(subj[i]);
        free(subj);
    }
    return NULL;
}
