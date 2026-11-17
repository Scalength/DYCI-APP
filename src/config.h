#ifndef CONFIG_H
#define CONFIG_H

/*
 * Production settings for the enrollment app.
 *
 * ── Endpoint ─────────────────────────────────────────────────────────
 * The Web App URL from your Apps Script deployment (ends in /exec).
 * At runtime the app first looks for an `endpoint.txt` file NEXT TO THE
 * EXECUTABLE — if it contains a https:// line, that wins. That way you
 * can change the URL on a staff PC without rebuilding.
 * If no endpoint.txt exists, the value below is used.
 */

#define SHEETS_ENDPOINT "https://script.google.com/macros/s/AKfycbwaPoBoofp_CTdx5GSQHXa_n8tY3Z0X8ii8e9xu_7unIduqYnoyPTC7Tyq-9GL9Js5OMw/exec"

/*
 * ── App version ──────────────────────────────────────────────────────
 * Shown in the window title and written to app.log.
 */
#define APP_VERSION "3.1"

/*
 * ── Expected spreadsheet-script version ─────────────────────────────
 * Every Code.gs reply carries "ver":"vN". After each submission the app
 * compares it with the value below and warns on the status line when
 * the deployed script is older — an old script is what ignores Student
 * ID, re-creates DATE 2 and writes subjects without labels. Bump this
 * whenever you paste a newer Code.gs.
 */
#define SHEETS_SCRIPT_VER "v13"

/*
 * ── Shared secret (optional, recommended) ────────────────────────────
 * If you set a non-empty SECRET in apps_script/Code.gs, put the SAME
 * string here and rebuild — POSTs without the matching token are
 * rejected server-side. This stops random people who find the URL from
 * writing into your sheet. (It is a lightweight deterrent, not real
 * security: the token ships inside the app and can be extracted by a
 * determined user. For an internal enrollment form that is fine.)
 * Leave empty ("") while SECRET in Code.gs is also empty.
 */
#define APP_TOKEN ""

#endif /* CONFIG_H */
