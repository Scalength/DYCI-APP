/*
 * test_subjects.c — unit tests for the editable subject catalog:
 * defaults, normalize, add/dup/remove, and the ini save/load roundtrip.
 * Run with: make test
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "subjects.h"

static void test_defaults(void)
{
    SubjectCatalog cat;
    subjects_defaults(&cat);

    /* every program-year must have at least 6 subjects, all uppercase */
    for (int p = 0; p < PROGRAM_COUNT; p++) {
        for (int y = 1; y <= YEAR_COUNT; y++) {
            SubjectList *l = subjects_list(&cat, p, y);
            assert(l->count >= 6 && l->count <= MAX_SUBJECTS);
            for (int i = 0; i < l->count; i++)
                assert(l->items[i][0] != '\0');
        }
    }
    assert(strcmp(PROGRAM_NAMES[0], "BSMaRE") == 0);
    assert(strcmp(PROGRAM_NAMES[1], "BSMT") == 0);

    SubjectList *bsmt1 = subjects_list(&cat, 1, 1);
    int found = 0;
    for (int i = 0; i < bsmt1->count; i++)
        if (strcmp(bsmt1->items[i], "COLGRES") == 0)
            found = 1;
    assert(found);
}

static void test_normalize(void)
{
    char out[SUBJECT_NAME_MAX];
    subjects_normalize(out, sizeof out, "  nav  2 ");
    assert(strcmp(out, "NAV 2") == 0);

    subjects_normalize(out, sizeof out, "pathfit \t 4");
    assert(strcmp(out, "PATHFIT 4") == 0);

    subjects_normalize(out, sizeof out, "   ");
    assert(out[0] == '\0');
}

static void test_add_remove(void)
{
    SubjectList l = {0};

    assert(subjects_add(&l, "NAV 3") == 0);
    assert(subjects_add(&l, " nav  3 ") == 1); /* duplicate after normalize */
    assert(subjects_add(&l, "") == -1);
    assert(subjects_add(&l, "NGEC 12") == 0);
    assert(l.count == 2);
    assert(strcmp(l.items[0], "NAV 3") == 0);
    assert(strcmp(l.items[1], "NGEC 12") == 0);

    assert(subjects_remove(&l, 0) == 0);
    assert(l.count == 1);
    assert(strcmp(l.items[0], "NGEC 12") == 0);
    assert(subjects_remove(&l, 5) == -1);

    /* full list */
    SubjectList full = {0};
    char name[32];
    for (int i = 0; i < MAX_SUBJECTS; i++) {
        snprintf(name, sizeof name, "SUBJ %d", i);
        assert(subjects_add(&full, name) == 0);
    }
    assert(subjects_add(&full, "ONE MORE") == -1);
}

static void test_roundtrip(void)
{
    const char *path = "test_subjects_tmp.ini";
    SubjectCatalog cat, reloaded;
    char err[256];

    subjects_defaults(&cat);
    /* customize */
    SubjectList *l = subjects_list(&cat, 0, 2);
    assert(subjects_add(l, "CUSTOM 99") == 0);
    assert(subjects_remove(l, 0) == 0);

    assert(subjects_save(&cat, path, err, sizeof err) == 0);
    assert(subjects_load(&reloaded, path, err, sizeof err) == 0);

    for (int p = 0; p < PROGRAM_COUNT; p++) {
        for (int y = 1; y <= YEAR_COUNT; y++) {
            SubjectList *a = subjects_list(&cat, p, y);
            SubjectList *b = subjects_list(&reloaded, p, y);
            assert(a->count == b->count);
            for (int i = 0; i < a->count; i++)
                assert(strcmp(a->items[i], b->items[i]) == 0);
        }
    }
    assert(subjects_list(&reloaded, 0, 2)->count ==
           subjects_list(&cat, 0, 2)->count);
    /* the removed default is gone, the custom one is there */
    SubjectList *l2 = subjects_list(&reloaded, 0, 2);
    int has_custom = 0, has_first_default = 0;
    for (int i = 0; i < l2->count; i++) {
        if (strcmp(l2->items[i], "CUSTOM 99") == 0) has_custom = 1;
        if (strcmp(l2->items[i], "AUTO 2") == 0) has_first_default = 1;
    }
    assert(has_custom && !has_first_default);

    remove(path);
}

static void test_bad_file_falls_back(void)
{
    const char *path = "test_subjects_bad.ini";
    FILE *f = fopen(path, "w");
    assert(f);
    fprintf(f, "[GARBAGE-9]\nOOPS\n");
    fclose(f);

    SubjectCatalog cat;
    char err[256];
    assert(subjects_load(&cat, path, err, sizeof err) == -1);
    /* catalog reset to defaults, message filled */
    assert(err[0] != '\0');
    SubjectList *l = subjects_list(&cat, 0, 1);
    assert(l->count >= 6);
    remove(path);
}

int main(void)
{
    test_defaults();
    test_normalize();
    test_add_remove();
    test_roundtrip();
    test_bad_file_falls_back();
    printf("All subjects tests passed.\n");
    return 0;
}
