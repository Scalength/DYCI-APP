/*
 * test_enrollment.c — dependency-free unit tests for validation,
 * name formatting, and JSON building (v2). Run with: make test
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "enrollment.h"

static Enrollment make_valid(void)
{
    Enrollment e = {0};
    snprintf(e.submitted_at,   sizeof e.submitted_at,   "2026-01-01 08:00:00");
    snprintf(e.student_id,     sizeof e.student_id,     "2026-0001");
    snprintf(e.status1,        sizeof e.status1,        "%s", STATUS1_OLD);
    snprintf(e.status2,        sizeof e.status2,        "%s", STATUS2_REGULAR);
    snprintf(e.last_name,      sizeof e.last_name,      "Dela Cruz");
    snprintf(e.first_name,     sizeof e.first_name,     "Juan");
    snprintf(e.middle_initial, sizeof e.middle_initial, "M");
    snprintf(e.gender,         sizeof e.gender,         "%s", GENDER_MALE);
    snprintf(e.course,         sizeof e.course,         "%s", "BSMT");
    snprintf(e.year_level,     sizeof e.year_level,     "1");
    snprintf(e.section,        sizeof e.section,        "A");
    snprintf(e.contact_number, sizeof e.contact_number, "09171234567");
    snprintf(e.committee,      sizeof e.committee,      "Committee 1");
    snprintf(e.subjects[0].name, sizeof e.subjects[0].name, "NAV 2");
    e.subjects[0].enrolled = 1;
    snprintf(e.subjects[1].name, sizeof e.subjects[1].name, "ICT");
    e.subjects[1].enrolled = 0;
    e.subject_count = 2;
    return e;
}

static void test_valid_passes(void)
{
    Enrollment e = make_valid();
    char err[ENROLLMENT_ERR_MAX] = "untouched";
    assert(enrollment_validate(&e, err, sizeof err) == 0);
    assert(err[0] == '\0');
}

static void test_required_fields(void)
{
    char err[ENROLLMENT_ERR_MAX];

    Enrollment a = make_valid();
    a.last_name[0] = ' ';
    a.last_name[1] = '\0';
    assert(enrollment_validate(&a, err, sizeof err) != 0);
    assert(strstr(err, "last name") != NULL);

    Enrollment b = make_valid();
    b.first_name[0] = '\0';
    assert(enrollment_validate(&b, err, sizeof err) != 0);

    Enrollment c = make_valid();
    c.section[0] = '\0';
    assert(enrollment_validate(&c, err, sizeof err) != 0);

    Enrollment d = make_valid();
    d.committee[0] = '\0';
    assert(enrollment_validate(&d, err, sizeof err) != 0);
}

static void test_choices_validated(void)
{
    char err[ENROLLMENT_ERR_MAX];

    Enrollment a = make_valid();
    snprintf(a.gender, sizeof a.gender, "OTHER");
    assert(enrollment_validate(&a, err, sizeof err) != 0);

    Enrollment c = make_valid();
    snprintf(c.year_level, sizeof c.year_level, "4");
    assert(enrollment_validate(&c, err, sizeof err) != 0);

    Enrollment d = make_valid();
    snprintf(d.course, sizeof d.course, "BSIT");
    assert(enrollment_validate(&d, err, sizeof err) != 0);
}

static void test_status_choices(void)
{
    char err[ENROLLMENT_ERR_MAX];

    /* both dropdowns are required */
    Enrollment a = make_valid();
    a.status1[0] = '\0';
    assert(enrollment_validate(&a, err, sizeof err) != 0);
    assert(strstr(err, "STATUS 1") != NULL);

    Enrollment b = make_valid();
    b.status2[0] = '\0';
    assert(enrollment_validate(&b, err, sizeof err) != 0);
    assert(strstr(err, "STATUS 2") != NULL);

    /* legacy values from the old STATUS columns are rejected */
    Enrollment c = make_valid();
    snprintf(c.status1, sizeof c.status1, "ENROLLED");
    assert(enrollment_validate(&c, err, sizeof err) != 0);

    Enrollment d = make_valid();
    snprintf(d.status2, sizeof d.status2, "NOT ENROLLED");
    assert(enrollment_validate(&d, err, sizeof err) != 0);

    /* every allowed combination passes */
    const char *s1[] = { STATUS1_OLD, STATUS1_NEW };
    const char *s2[] = { STATUS2_REGULAR, STATUS2_IRREGULAR };
    for (size_t i = 0; i < sizeof s1 / sizeof s1[0]; i++) {
        for (size_t j = 0; j < sizeof s2 / sizeof s2[0]; j++) {
            Enrollment e = make_valid();
            snprintf(e.status1, sizeof e.status1, "%s", s1[i]);
            snprintf(e.status2, sizeof e.status2, "%s", s2[j]);
            assert(enrollment_validate(&e, err, sizeof err) == 0);
        }
    }
}

static void test_student_id(void)
{
    char err[ENROLLMENT_ERR_MAX];

    /* required */
    Enrollment a = make_valid();
    a.student_id[0] = '\0';
    assert(enrollment_validate(&a, err, sizeof err) != 0);
    assert(strstr(err, "required") != NULL);

    /* free-form: any scheme passes — nothing is reshaped or rejected */
    const char *ok[] = {
        "2026-0000",   /* classic 4-4 */
        "2026-053043", /* year + 6 digits */
        "ABC-12345",   /* letters */
        "S/N 0092",    /* slash and spaces */
        "12345",       /* bare number */
    };
    for (size_t i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        Enrollment b = make_valid();
        snprintf(b.student_id, sizeof b.student_id, "%s", ok[i]);
        assert(enrollment_validate(&b, err, sizeof err) == 0);
    }
}

static void test_contact_rules(void)
{
    char err[ENROLLMENT_ERR_MAX];

    Enrollment a = make_valid();
    snprintf(a.contact_number, sizeof a.contact_number, "123456"); /* 6 digits */
    assert(enrollment_validate(&a, err, sizeof err) != 0);
    assert(strstr(err, "7 to 15") != NULL);

    Enrollment b = make_valid();
    snprintf(b.contact_number, sizeof b.contact_number, "abc-def");
    assert(enrollment_validate(&b, err, sizeof err) != 0);

    Enrollment c = make_valid();
    snprintf(c.contact_number, sizeof c.contact_number, "+63 (917) 123-4567");
    assert(enrollment_validate(&c, err, sizeof err) == 0);
}

static void test_middle_initial(void)
{
    char err[ENROLLMENT_ERR_MAX];

    Enrollment a = make_valid();
    snprintf(a.middle_initial, sizeof a.middle_initial, "XY");
    assert(enrollment_validate(&a, err, sizeof err) != 0);

    Enrollment b = make_valid();
    b.middle_initial[0] = '\0'; /* optional */
    assert(enrollment_validate(&b, err, sizeof err) == 0);

    Enrollment c = make_valid();
    snprintf(c.middle_initial, sizeof c.middle_initial, "m.");
    assert(enrollment_validate(&c, err, sizeof err) == 0);
}

static void test_name_format(void)
{
    Enrollment e = make_valid();
    char out[256];

    enrollment_format_name(&e, out, sizeof out);
    assert(strcmp(out, "Dela Cruz, Juan M.") == 0);

    e.middle_initial[0] = '\0';
    enrollment_format_name(&e, out, sizeof out);
    assert(strcmp(out, "Dela Cruz, Juan") == 0);
}

static void test_json(void)
{
    Enrollment e = make_valid();
    char *json = enrollment_to_json(&e);
    assert(json != NULL);
    assert(strcmp(json,
        "{\"submitted_at\":\"2026-01-01 08:00:00\","
        "\"student_id\":\"2026-0001\","
        "\"status1\":\"OLD\",\"status2\":\"REGULAR\","
        "\"name\":\"Dela Cruz, Juan M.\","
        "\"gender\":\"MALE\",\"course\":\"BSMT\",\"year_level\":\"1\","
        "\"section\":\"A\",\"contact_number\":\"09171234567\","
        "\"committee\":\"Committee 1\",\"highlight\":\"\","
        "\"subjects\":[{\"name\":\"NAV 2\",\"status\":\"ENROLLED\"},"
        "{\"name\":\"ICT\",\"status\":\"NOT ENROLLED\"}]}") == 0);
    free(json);

    /* highlight rides along when picked (NONE is the app default) */
    Enrollment y = make_valid();
    snprintf(y.highlight, sizeof y.highlight, "%s", HIGHLIGHT_YELLOW);
    json = enrollment_to_json(&y);
    assert(strstr(json, "\"highlight\":\"YELLOW\"") != NULL);
    free(json);

    Enrollment n2 = make_valid();
    snprintf(n2.highlight, sizeof n2.highlight, "%s", HIGHLIGHT_NONE);
    json = enrollment_to_json(&n2);
    assert(strstr(json, "\"highlight\":\"NONE\"") != NULL);
    free(json);

    /* JSON escaping still applies to subject names */
    Enrollment w = make_valid();
    snprintf(w.subjects[0].name, sizeof w.subjects[0].name, "PATH \"X\"");
    w.subjects[1].enrolled = 1;
    json = enrollment_to_json(&w);
    assert(strstr(json,
        "[{\"name\":\"PATH \\\"X\\\"\",\"status\":\"ENROLLED\"},"
        "{\"name\":\"ICT\",\"status\":\"ENROLLED\"}]") != NULL);
    free(json);

    /* zero subjects -> empty array */
    Enrollment none = make_valid();
    none.subject_count = 0;
    json = enrollment_to_json(&none);
    assert(strstr(json, "\"subjects\":[]") != NULL);
    free(json);
}

int main(void)
{
    test_valid_passes();
    test_required_fields();
    test_choices_validated();
    test_status_choices();
    test_student_id();
    test_contact_rules();
    test_middle_initial();
    test_name_format();
    test_json();
    printf("All enrollment tests passed.\n");
    return 0;
}
