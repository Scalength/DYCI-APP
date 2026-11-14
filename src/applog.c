/*
 * applog.c — implementation of the file logger (see applog.h).
 *
 * The log is opened in append mode per line. Enrollment submissions run
 * in a worker thread while the UI thread may log too; short single-line
 * appends through separate fopen("a") calls are close enough to atomic
 * for a single-user desktop tool.
 */
#define _POSIX_C_SOURCE 200809L /* localtime_r under -std=c11 */
#include "applog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static char log_path[600];
static int  log_enabled = 0;

void applog_open(const char *directory)
{
    log_enabled = 0;

    if (!directory || directory[0] == '\0') {
        strcpy(log_path, "app.log");
    } else if (snprintf(log_path, sizeof log_path, "%sapp.log",
                        directory) >= (int)sizeof log_path) {
        return; /* path too long — give up quietly */
    }

    FILE *fp = fopen(log_path, "a");
    if (!fp)
        return; /* not writable (e.g. locked dir) — stay silent */
    fclose(fp);

    log_enabled = 1;
    applog("---- Enrollment App started ----");
}

void applog(const char *fmt, ...)
{
    if (!log_enabled)
        return;

    FILE *fp = fopen(log_path, "a");
    if (!fp)
        return;

    time_t now = time(NULL);
    struct tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif
    char stamp[32];
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tm_now);
    fprintf(fp, "[%s] ", stamp);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);

    fputc('\n', fp);
    fclose(fp);
}
