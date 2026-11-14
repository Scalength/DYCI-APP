#ifndef SUBJECTS_H
#define SUBJECTS_H

#include <stddef.h>

#define SUBJECT_NAME_MAX 64
#define MAX_SUBJECTS     48   /* per program-year */
#define PROGRAM_COUNT    2
#define YEAR_COUNT       3    /* Year 1..3 */
#define SUBJECTS_INI_NAME "subjects.ini"

/* Program display names; index order is used everywhere. */
extern const char *const PROGRAM_NAMES[PROGRAM_COUNT];

/* One editable subject list for a program-year. */
typedef struct {
    char items[MAX_SUBJECTS][SUBJECT_NAME_MAX];
    int count;
} SubjectList;

typedef struct {
    SubjectList years[YEAR_COUNT];   /* index 0 = Year 1 */
} Program;

typedef struct {
    Program programs[PROGRAM_COUNT]; /* index = PROGRAM_NAMES index */
} SubjectCatalog;

/* Fill the catalog with the built-in defaults. */
void subjects_defaults(SubjectCatalog *cat);

/* year is 1..3. Returns pointer to the list for that program-year. */
SubjectList *subjects_list(SubjectCatalog *cat, int program_idx, int year_idx);

/* Trim, collapse spaces, uppercase: "  nav  2 " -> "NAV 2". */
void subjects_normalize(char *dst, size_t dst_cap, const char *src);

/* Returns 0 on success, 1 if the (normalized) name already exists,
 * -1 if the list is full or the name is empty. */
int subjects_add(SubjectList *list, const char *name);

/* Returns 0 on success, -1 if index is out of range. */
int subjects_remove(SubjectList *list, int index);

/* INI-style file:
 *   [BSMaRE-1]
 *   THERMO
 *   MECH
 *   ...
 * Lines starting with '#' are comments. On success returns 0.
 * On failure returns -1 and writes a message into err (catalog content
 * is then reset to defaults so the caller can keep running). */
int subjects_save(const SubjectCatalog *cat, const char *path,
                  char *err, size_t err_len);
int subjects_load(SubjectCatalog *cat, const char *path,
                  char *err, size_t err_len);

#endif /* SUBJECTS_H */
