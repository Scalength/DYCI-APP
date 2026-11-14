/*
 * subjects.c — the editable subject catalog (two programs x three years),
 * persisted in a small INI file next to the executable.
 */
#include "subjects.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

const char *const PROGRAM_NAMES[PROGRAM_COUNT] = { "BSMaRE", "BSMT" };

/* ---------------- built-in defaults ---------------- */

static const char *const DEF_BSMARE_1[] = {
    "THERMO", "MECH", "MACH 2", "DRAW", "NGEC 1", "NGEC 4", "PATHFIT 2", NULL
};
static const char *const DEF_BSMARE_2[] = {
    "AUTO 2", "AUX MACH 2", "NAV ARCH", "MAR ENV", "MARLAW", "ICT", "PATHFIT 4", NULL
};
static const char *const DEF_BSMARE_3[] = {
    "MAINT", "EWATCH", "NGEC 2", "NGEC 6", "NGEC 8", "NGEC 11", "RIZAL", NULL
};
static const char *const DEF_BSMT_1[] = {
    "NAV 2", "SEAM 2", "COLGRES", "MET-O", "RESEARCH (ELECTIVE)", "PATHFIT 2", NULL
};
static const char *const DEF_BSMT_2[] = {
    "NAV 1", "NAV 5", "SEAM 4", "MARCOM", "MGMT", "NGEC 8", "PATHFIT 4", NULL
};
static const char *const DEF_BSMT_3[] = {
    "DECK WATCH", "NAV 7", "SEAM 6", "NGEC 2", "NGEC 6", "NGEC 11", "RIZAL", NULL
};

static const char *const *const DEFAULT_SETS[PROGRAM_COUNT][YEAR_COUNT] = {
    { DEF_BSMARE_1, DEF_BSMARE_2, DEF_BSMARE_3 },
    { DEF_BSMT_1,   DEF_BSMT_2,   DEF_BSMT_3   },
};

void subjects_defaults(SubjectCatalog *cat)
{
    memset(cat, 0, sizeof *cat);
    for (int p = 0; p < PROGRAM_COUNT; p++) {
        for (int y = 0; y < YEAR_COUNT; y++) {
            SubjectList *list = &cat->programs[p].years[y];
            for (const char *const *s = DEFAULT_SETS[p][y]; *s; s++) {
                if (list->count < MAX_SUBJECTS) {
                    subjects_normalize(list->items[list->count],
                                       SUBJECT_NAME_MAX, *s);
                    list->count++;
                }
            }
        }
    }
}

SubjectList *subjects_list(SubjectCatalog *cat, int program_idx, int year_idx)
{
    if (program_idx < 0 || program_idx >= PROGRAM_COUNT)
        program_idx = 0;
    if (year_idx < 1 || year_idx > YEAR_COUNT)
        year_idx = 1;
    return &cat->programs[program_idx].years[year_idx - 1];
}

/* ---------------- helpers ---------------- */

void subjects_normalize(char *dst, size_t dst_cap, const char *src)
{
    size_t o = 0;
    int prev_space = 1; /* swallow leading spaces */

    if (dst_cap == 0)
        return;

    for (; *src; src++) {
        unsigned char c = (unsigned char)*src;
        if (isspace(c)) {
            if (!prev_space && o < dst_cap - 1)
                dst[o++] = ' ';
            prev_space = 1;
        } else {
            if (o < dst_cap - 1)
                dst[o++] = (char)toupper(c);
            prev_space = 0;
        }
    }
    if (o > 0 && dst[o - 1] == ' ')
        o--;
    dst[o] = '\0';
}

static int list_contains(const SubjectList *list, const char *norm_name)
{
    for (int i = 0; i < list->count; i++)
        if (strcmp(list->items[i], norm_name) == 0)
            return 1;
    return 0;
}

int subjects_add(SubjectList *list, const char *name)
{
    char norm[SUBJECT_NAME_MAX];
    subjects_normalize(norm, sizeof norm, name);
    if (norm[0] == '\0')
        return -1;
    if (list_contains(list, norm))
        return 1;
    if (list->count >= MAX_SUBJECTS)
        return -1;
    snprintf(list->items[list->count], SUBJECT_NAME_MAX, "%s", norm);
    list->count++;
    return 0;
}

int subjects_remove(SubjectList *list, int index)
{
    if (index < 0 || index >= list->count)
        return -1;
    for (int i = index; i < list->count - 1; i++)
        memcpy(list->items[i], list->items[i + 1], SUBJECT_NAME_MAX);
    list->count--;
    return 0;
}

/* ---------------- file I/O ---------------- */

static void write_list(FILE *f, const SubjectList *list)
{
    for (int i = 0; i < list->count; i++)
        fprintf(f, "%s\n", list->items[i]);
}

int subjects_save(const SubjectCatalog *cat, const char *path,
                  char *err, size_t err_len)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        snprintf(err, err_len, "Cannot write %s (is the folder writable?)", path);
        return -1;
    }
    fprintf(f, "# Enrollment app subject lists. One subject per line.\n");
    fprintf(f, "# Sections: BSMaRE-1..3 and BSMT-1..3. Delete this file to\n");
    fprintf(f, "# regenerate the defaults.\n");
    for (int p = 0; p < PROGRAM_COUNT; p++) {
        for (int y = 0; y < YEAR_COUNT; y++) {
            fprintf(f, "\n[%s-%d]\n", PROGRAM_NAMES[p], y + 1);
            write_list(f, &cat->programs[p].years[y]);
        }
    }
    if (fclose(f) != 0) {
        snprintf(err, err_len, "Error while closing %s", path);
        return -1;
    }
    return 0;
}

static int ci_equal(const char *a, const char *b)
{
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int parse_section(const char *line, int *prog, int *year)
{
    /* Expect "[<Program>-<year>]" */
    size_t n = strlen(line);
    if (n < 4 || line[0] != '[' || line[n - 1] != ']')
        return -1;

    char body[SUBJECT_NAME_MAX];
    snprintf(body, sizeof body, "%.*s", (int)(n - 2), line + 1);

    char *dash = strrchr(body, '-');
    if (!dash)
        return -1;
    *dash = '\0';
    const char *ys = dash + 1;

    int p = -1;
    for (int i = 0; i < PROGRAM_COUNT; i++)
        if (ci_equal(body, PROGRAM_NAMES[i]))
            p = i;
    if (p < 0 || ys[0] < '1' || ys[0] > '3' || ys[1] != '\0')
        return -1;

    *prog = p;
    *year = ys[0] - '0';
    return 0;
}

static void load_fail(SubjectCatalog *cat, char *err, size_t err_len,
                      const char *msg, const char *path)
{
    subjects_defaults(cat);
    snprintf(err, err_len, "%s\n(%s)\nLoading built-in defaults instead. "
             "Delete the file to regenerate it.", msg, path);
}

int subjects_load(SubjectCatalog *cat, const char *path,
                  char *err, size_t err_len)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        /* First run: create the file from defaults. */
        subjects_defaults(cat);
        if (subjects_save(cat, path, err, err_len) != 0)
            return -1;
        err[0] = '\0';
        return 0;
    }

    memset(cat, 0, sizeof *cat); /* the file is the single source of truth */

    char line[512];
    int prog = -1, year = 0, sections = 0;
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        p[strcspn(p, "\r\n")] = '\0';
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0' || *p == '#')
            continue;

        if (*p == '[') {
            if (parse_section(p, &prog, &year) != 0) {
                load_fail(cat, err, err_len,
                          "subjects.ini has an unknown section (expected "
                          "[BSMaRE-1] .. [BSMT-3])", path);
                fclose(f);
                return -1;
            }
            sections++;
            continue;
        }

        if (prog < 0) {
            load_fail(cat, err, err_len,
                      "subjects.ini has subjects before any [section]", path);
            fclose(f);
            return -1;
        }

        SubjectList *list = subjects_list(cat, prog, year);
        int rc = subjects_add(list, p); /* dup or overfull entries are skipped */
        (void)rc;
    }
    fclose(f);

    if (sections == 0) {
        load_fail(cat, err, err_len, "subjects.ini contains no sections", path);
        return -1;
    }

    err[0] = '\0';
    return 0;
}
