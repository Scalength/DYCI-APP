/*
 * applog.h — tiny file logger for production diagnostics.
 *
 * Writes lines like
 *     [2026-11-06 21:33:12] submit result: HTTP 200
 * to `app.log` next to the executable. Because a released app has no
 * console window, this file is the only way to see what happened on a
 * staff PC when something goes wrong.
 *
 * Not used by the unit tests; kept free of GTK/curl includes.
 */
#ifndef APPLOG_H
#define APPLOG_H

/* Point the log at <directory>/app.log (directory may be "" for CWD).
 * Safe to call once at startup; if the file is not writable, logging
 * silently disables itself — the app must never crash because of it. */
void applog_open(const char *directory);

/* printf-style append. No-op before applog_open or if logging failed. */
void applog(const char *fmt, ...);

#endif /* APPLOG_H */
