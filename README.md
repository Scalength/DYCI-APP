# DYCI CME APP v3.1 (C + GTK3 → Google Sheets)

A native desktop window (written in C) for enrollment committees.
Branded **DYCI CME APP**: the DYCI seal is embedded in the executable
(nothing extra to ship or lose) and appears as the small window /
taskbar icon. Since v2.6 the form itself has **no big logo and no big
title** — the fields start right at the top of the clean white window.

1. Fill in **Student information**: Student ID, then **Status 1
   (OLD / NEW)** and **Status 2 (REGULAR / IRREGULAR)** — two dropdowns
   right below the Student ID box — then Last name / First name /
   Middle initial, Gender, Course, Year level, Section, Contact number,
   Enrollment committee. Student ID is a plain text box — type it
   exactly as your office issues it (e.g. `2026-0000`, `2026-053043`);
   nothing is reformatted or rejected. Both Status picks are required.
   Optionally pick a **Highlight** (NONE / YELLOW / GREEN): the
   student's whole row gets that fill in the sheet — e.g. yellow while
   a record has an error, green once it is resolved.
2. Pick **Course + Year level** — the subject list for that program-year
   appears automatically, each subject with its own
   **ENROLLED / NOT ENROLLED** dropdown.
3. **Submit** — one row lands in the matching sheet tab (one tab per
   section, e.g. `A1A`).

Subject lists are editable in-app (add/remove per program-year) and persist
in a `subjects.ini` file next to the executable. Defaults are the six
program-year lists below.

```
┌──────────────────────────┐  HTTPS POST (JSON, libcurl)
│  GTK3 window (main.c)    │ ──────────────────────────────►  Apps Script
│  enrollment.c (validate) │ ◄──────────────────────────────  Web App
│  subjects.c   (catalog)  │        {"status":"ok"}              │
│  sheets_gateway.c (curl) │                                     ▼
└──────────────────────────┘    One tab per section, named after the
                                 section alone: `A1A`, `A1B`, … — each
                                 with its own SUBJECT TALLY block
                                 (per-subject counts, right of the data)
```

## Sheet layout

Rows go to the tab named after the SECTION alone (e.g. `A1A`, `A1B`);
the tab is created on first submission. **New in v3.1 (Code.gs v13):**
the tab name is the section alone — no more `BSMT-1 |` prefix. Every
section still gets its own rows and its own 40-slot quota — A1A and
A1B never share a tab or a limit. **Note:** two course-years that use
the SAME section name (e.g. BSMaRE-1 A1A and BSMT-1 A1A) now share one
`A1A` tab — and with it the quota of any subject both years have in
common (PATHFIT 2, NGEC 8, …). The rows keep their COURSE and YEAR
LEVEL columns, and each tab's SUBJECT TALLY picks its subject lists
from the course-year values in its rows. Tabs from the older layouts
(`BSMT-1`, `BSMaRE-1`, `BSMT-1 | A1A`, …) are kept exactly as they
are: new submissions never write into them, and the menu fixer still
heals them if needed. Columns:

| # | Column | Source |
|---|---|---|
| 1 | DATE | submission timestamp (auto) |
| 2 | STUDENT ID | free text, exactly as typed in the app (e.g. `2026-0000`, `2026-053043`) |
| 3 | NAME | `Last, First M.I.` (composed by the app) |
| 4 | GENDER | MALE / FEMALE |
| 5 | COURSE | BSMaRE / BSMT |
| 6 | YEAR LEVEL | 1 / 2 / 3 |
| 7 | SECTION | typed (e.g. `A1A`) — also names the tab |
| 8 | CONTACT NUMBER | typed (7–15 digits) |
| 9 | ENROLLMENT COMMITTEE | typed |
| 10 | STATUS 1 | picked in the app: OLD / NEW (sheet dropdown too) |
| 11 | STATUS 2 | picked in the app: REGULAR / IRREGULAR (sheet dropdown too) |
| 12+ | one column per **subject**, cell = ENROLLED / NOT ENROLLED | created on demand |

Columns only ever grow: the first submission for a tab creates a column
for every subject on that program-year's list, and a subject added later
in the app gets its column on the next submission. Each subject cell
shows what was picked in the app — ENROLLED or NOT ENROLLED — and the
subject columns, STATUS 1 and STATUS 2 all carry real **dropdowns**
(data validation), so the sheet looks and behaves like the app's
interface. STATUS 1 and STATUS 2 arrive filled from the app's two
dropdowns under the Student ID box; the committee can still correct a
cell later by picking the other value in the sheet.

Upgrading an old tab? A submission heals the tab it writes to before
the row is added — **on demand** via the menu, and since v6 **even a
duplicate submission finishes the repair** (the healing runs before the
duplicate guard, so re-submitting a student who is already logged today
still fixes the tab; it just doesn't add a second row). Legacy tabs —
per-year (`BSMT-1`) and the 2026-11 `Course-Year | Section` style
(`BSMT-1 | A1A`) — are never written by new submissions, so heal them
with the menu: once the current Code.gs is saved in the Apps Script
editor, the spreadsheet shows a menu **DYCI CME APP → Fix columns &
dropdowns now** that heals EVERY tab immediately — per-year and
per-section alike — with no submission needed and no redeploy needed.
Healing does the following:

- the **DATE 2** column is dropped (DATE keeps the one timestamp);
- **`[OBJECT OBJECT]` subject columns** (left behind by an older
  app/server mix-up) are **rescued, not just deleted**: when their
  count matches the subject list for that tab, they are rebuilt as
  properly named subject columns at the end of the sheet, the old
  checkmark values are converted (`TRUE / YES / Y / X / 1 / ✓` →
  ENROLLED, blank / `FALSE / NO / N / 0` → NOT ENROLLED), and only then
  are the junk columns removed. If the count does not match, the junk
  is removed and the next submission recreates the columns from the
  app's list;
- the legacy STATUS 1 / STATUS 2 columns (the old ENROLLED / NOT
  ENROLLED kind) are replaced by fresh ones — old values in them are
  cleared — while columns already holding OLD / NEW / REGULAR /
  IRREGULAR data are kept;
- a STUDENT ID column is inserted after DATE if missing, and STATUS 1 /
  STATUS 2 are created after ENROLLMENT COMMITTEE if missing;
- every dropdown (STATUS 1, STATUS 2, all subject columns) is
  re-applied.

Running the fix twice changes nothing (it is idempotent) and a summary
toast reports what was fixed per tab.

## Subject tally on every tab + the 40-per-section limit (v11; per section since v12, section-named tabs since v13)

Every tab carries its own **SUBJECT TALLY** block, written to the
RIGHT of the data columns with one blank column in between (on a fresh
`A1A` tab with the 6 BSMT-1 subjects that is columns U–Y). One row per
subject of that tab:

| SUBJECT | ENROLLED | NOT ENROLLED | SLOTS LEFT | STATUS |
|---|---|---|---|---|
| THERMO | 40 | 12 | 0 | FULL |
| MECH | 17 | 3 | 23 | OPEN |

- Each tab counts only itself — **every section tab keeps an
  independent tally** of its own subjects: A1A and A1B never share
  counts. The subjects a tab tallies are picked from the COURSE +
  YEAR LEVEL values in its rows, so a tab shared by two course-years
  (same section name) tallies the union of both lists.
- **ENROLLED / NOT ENROLLED** count the student rows of that tab whose
  subject cell holds that value; **SLOTS LEFT** is `40 − ENROLLED`.
- The block is rebuilt from the data after **every** submission and
  from the menu (**DYCI CME APP → Refresh subject tally**, which
  refreshes every tab at once), so the numbers always match the rows —
  including rows edited by hand. When a new subject column appears, the
  block moves one column right automatically. FULL rows are painted
  red; rows down to their last 5 slots are painted yellow.
- **40 is a hard limit** (`SUBJECT_LIMIT = 40` in Code.gs): when a
  subject the app marks ENROLLED is already at 40 on that tab, the
  submission is REJECTED — nothing is written — and the app shows the
  reason in red on its status line:
  “Not saved — the spreadsheet rejected this submission. Reason:
  LIMIT REACHED: NAV 2 is full (40/40 ENROLLED) on A1A. No row
  was added. …” The form is not cleared, so the pick can be changed and
  resent. NOT ENROLLED entries never count against the cap, and every
  section tab has its own quota — `A1A` filling NAV 2 does not
  affect `A1B` (put the next student in A1B and the subject
  has its 40 slots again). Remember: a course-year that reuses the
  SAME section name shares that tab — and the quota of any subject
  both years have in common. The script serializes
  submissions with LockService, so two simultaneous posts cannot both
  slip past the limit.
- To change the capacity or disable it: edit `SUBJECT_LIMIT = 40` at
  the top of `Code.gs` (`0` disables the limit entirely), save, and
  deploy a new version (Manage deployments → ✏ → New version).
- Upgrading from v10? The old separate **SUBJECT TALLY** tab is deleted
  automatically on the next submission or menu fix — its numbers now
  live in the per-tab blocks.

## Default subject lists

| BSMaRE 1 | BSMaRE 2 | BSMaRE 3 | BSMT 1 | BSMT 2 | BSMT 3 |
|---|---|---|---|---|---|
| THERMO | AUTO 2 | MAINT | NAV 2 | NAV 1 | DECK WATCH |
| MECH | AUX MACH 2 | EWATCH | SEAM 2 | NAV 5 | NAV 7 |
| MACH 2 | NAV ARCH | NGEC 2 | COLGRES | SEAM 4 | SEAM 6 |
| DRAW | MAR ENV | NGEC 6 | MET-O | MARCOM | NGEC 2 |
| NGEC 1 | MARLAW | NGEC 8 | RESEARCH (ELECTIVE) | MGMT | NGEC 6 |
| NGEC 4 | ICT | NGEC 11 | PATHFIT 2 | NGEC 8 | NGEC 11 |
| PATHFIT 2 | PATHFIT 4 | RIZAL |  | PATHFIT 4 | RIZAL |

Names were normalized to UPPERCASE, and these typos in the request were
corrected: `NEGEC 2`→NGEC 2, `SEAM p2`→SEAM 2, `ELECTIVVE`→ELECTIVE,
`SEAM 6 NGEC 2`→SEAM 6 + NGEC 2, `NGECC 11`→NGEC 11. `NAV ,` (BSMT 2) was
read as **NAV 1** — fix it in the editor if it should be something else.
All of them are editable in the app anyway.

## Editing subjects

Click **Edit subjects…** while the desired Course/Year is selected:

- **Add subject** — type a name and press Enter or the button (normalized
  to uppercase, duplicates rejected, max 48 per program-year).
- **Remove** — deletes it from that program-year's list.
- Changes are saved to `subjects.ini` next to the executable immediately.
- Delete `subjects.ini` to regenerate the defaults.

Note: switching Course/Year rebuilds the list with every subject defaulting
to ENROLLED — set statuses *after* picking the program-year. The
**Mark all** buttons speed that up.

## Step 1 — Set up the Google Sheet backend (5 minutes)

1. Go to [sheets.new](https://sheets.new) (reuse your v1 spreadsheet is fine).
2. **Extensions → Apps Script**, replace everything with
   [`apps_script/Code.gs`](apps_script/Code.gs).
3. **Deploy → New deployment → Web app**: Execute as **Me**, access
   **Anyone** (the dialog defaults to "Only myself" — change it, or the
   app gets a Google login page instead of data). First time? Approve
   the authorization prompts — if Google says "hasn't verified this
   app", click **Advanced → Go to … (unsafe)**; it's your own script.
4. Copy the **Web App URL** (`.../exec`) and open it in a browser. It
   shows a **status page** with three things to check:
   - `— code v13` — anything else means the old code is still deployed;
   - **Bound spreadsheet: …** + URL — must be the sheet you are
     checking; if it names a different spreadsheet, the code was
     pasted into the wrong script project;
   - one line per enrollment tab, marked **OK** or **NEEDS FIX** with
     the exact problems still present (DATE 2, `[OBJECT OBJECT]`,
     missing STUDENT ID / subject columns).
   Since v7 every submission reply also carries a version tag (now
   `"ver":"v13"`) — the app compares it after each submit and warns on
   its status line when an older script answered.
5. **Already deployed v1–v4?** This is the step people miss: use
   **Deploy → Manage deployments → ✏ (pencil) → Version: New version →
   Deploy**. Clicking **New deployment** instead creates a *new URL* —
   the apps keep posting to the old `/exec` link, which keeps running
   the old code forever. Same deployment + New version = same URL,
   new code.
6. Open the spreadsheet again: there is now a **DYCI CME APP** menu at
   the top. Click **Fix columns & dropdowns now** once — it deletes
   DATE 2, converts the `[OBJECT OBJECT]` columns into properly named
   subject columns with ENROLLED / NOT ENROLLED dropdowns, and repairs
   every other tab in one go (no submission required).
7. Point the apps at the URL: paste it into `endpoint.txt` next to
   the .exe (Notepad is fine — first `http(s)://...` line wins). **No
   rebuild needed.** Editing the spreadsheet itself (renaming it, moving
   it to another Drive folder) changes nothing; only replacing the
   script or creating a different spreadsheet does.
8. Nothing else to set up: the script creates each section tab (named
   after the section alone — `A1A`, `A1B`, …), the header row and the
   dropdowns (STATUS 1,
   STATUS 2 and every subject column) automatically on the first
   submission.
   Since v8 it also formats every tab on each submit: bold
   **centered** header row, **left-aligned**, vertically centered data
   rows — including rows written earlier by an older script. (Want the
   data centered too? Change `CONTENT_HALIGN` in `Code.gs`.) Since v9
   it also paints rows from the **HIGHLIGHT** column (one dropdown:
   NONE / YELLOW / GREEN, added automatically): YELLOW / GREEN fill
   the whole row, NONE clears the fill, and a blank cell leaves the
   row alone — so painting rows by hand stays safe. Change a cell and
   the next submission (or the menu fixer) re-paints the tab. Since
   v11 every submission also rebuilds the tab's **SUBJECT TALLY** block
   and enforces the 40-per-subject hard limit (see the section above).

Optional hardening (recommended before distributing the app):

- In `Code.gs` set `const SECRET = 'your-secret'`, and set the same
  string as `APP_TOKEN` in `src/config.h`, then rebuild. The script now
  rejects posts without the token — so strangers who find your URL
  cannot write into the sheet. (It's a deterrent, not encryption: the
  token lives inside the app.)
- Duplicate guard: `BLOCK_DUPLICATES = true` (default) skips a second
  row for the same student (same STUDENT ID, or same NAME + SECTION for
  rows without an ID) on the same day. The app then shows an orange
  "already submitted today" note instead of a success message.

## Step 2 — Configure and build

Paste the `/exec` URL into **either** [`src/config.h`](src/config.h)
(rebuild required) **or** `endpoint.txt` next to the executable (no
rebuild — see `endpoint.txt.example`). Then:

```sh
make        # builds enrollment-app, test_gateway, test_unit, test_subjects
make test   # runs the unit tests
./enrollment-app
```

The DYCI seal (window/taskbar icon) is embedded: `src/logo_dyci.h`
(generated C bytes) + `src/logo_dyci.png`, produced from
`assets/dyci-logo.webp` by `tools/make_logo_header.py`. To swap the
logo later: drop the new file in (PNG/WebP with transparent background
recommended), run `python tools/make_logo_header.py
[path/to/new-logo]`, and rebuild — nothing else to update (no extra
runtime file exists). The seal is only used as the icon; the form
itself shows no logo.

Prerequisites:

- **Ubuntu / Debian**: `sudo apt install build-essential pkg-config make libgtk-3-dev libcurl4-openssl-dev`
- **Windows** ([MSYS2](https://www.msys2.org), UCRT64 shell):
  `pacman -S make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-pkg-config mingw-w64-ucrt-x86_64-gtk3 mingw-w64-ucrt-x86_64-curl`
- **macOS**: `brew install gtk+3 curl pkg-config make`

## Step 3 — Verify without the GUI

```sh
./test_gateway "Dela Cruz" "Juan" "M" "2026-0001" "MALE" "BSMT" "1" "A1A" \
               "09171234567" "Committee 1" "NAV 2,SEAM 2" "OLD" "REGULAR"
# Success (HTTP 200). Check the A1A tab — new row with STUDENT
# ID 2026-0001, STATUS 1 OLD, STATUS 2 REGULAR, and ENROLLED under NAV 2
# and SEAM 2. (The last two arguments are optional and default to
# OLD / REGULAR.)
```

## Step 4 — Production packaging: make it open-and-use

A bare `enrollment-app.exe` only runs on PCs that have GTK installed —
on any other machine, double-clicking it silently fails (missing
`libgtk-3-0.dll` etc.). Production-ready distribution fixes that by
shipping the runtime WITH the app:

### The one command (on your build PC)

```sh
make portable        # in the same MSYS2 UCRT64/MINGW64 shell you build in
```

This builds, runs the tests, then produces:

```
dist/EnrollmentApp/
├── enrollment-app.exe   GUI subsystem build — no console window
├── run.bat              ← staff double-click THIS (sets runtime paths)
├── START-HERE.txt       one-page instructions for staff
├── endpoint.txt         your /exec URL (ships pre-filled if present)
├── ca-bundle.crt        TLS trust store — REQUIRED for HTTPS on clean PCs
├── *.dll                every GTK3/curl DLL the app needs (~25 files)
├── lib/ share/ etc/     pixbuf loaders, GLib schemas, fontconfig
EnrollmentApp-Portable.zip   ← the whole folder, ready to send
```

Note on HTTPS: the bundled curl has no Windows trust store of its own.
The app therefore loads `ca-bundle.crt` from next to the exe (error
77 would appear without it). The packaging copies it from your MSYS2
installation automatically — just never delete it from the folder.

### Give it to users

1. Send `EnrollmentApp-Portable.zip` (email, USB, Google Drive).
2. They extract it anywhere and double-click **run.bat**.
   Nothing is installed, no admin rights needed; deleting the folder
   uninstalls it.
3. First launch on a new PC shows a blue **“Windows protected your
   PC”** SmartScreen dialog — that's because the exe is not digitally
   signed (a certificate costs hundreds of USD/year). Staff click
   **More info → Run anyway**; this is also written in START-HERE.txt.
   Antivirus may scan it the first time and let it through.
4. On the staff PC, put your `/exec` URL into `endpoint.txt` (one line)
   — no rebuilding needed if the URL ever changes.

### Everything that makes it “production” now

| Concern | How it's handled |
|---|---|
| Runs with nothing installed | All DLLs + GTK runtime files ship in the folder |
| No console window | `make` builds with `-mwindows` automatically on Windows |
| URL change without rebuild | `endpoint.txt` next to the exe overrides `config.h` |
| Spam into the sheet | optional `SECRET` (Code.gs) + `APP_TOKEN` (config.h) |
| Duplicate rows | server-side guard on STUDENT ID (or NAME + SECTION) per day |
| Diagnosing a staff PC | `app.log` next to the exe records every submit + error |
| Flaky Wi-Fi loses data | failed payloads are saved to `failed-submissions.jsonl` and shown as a failure in the app |
| HTTPS on clean PCs | portable `ca-bundle.crt` is shipped and loaded automatically |
| Wrong input | validation blocks empty names/sections, bad contact numbers, missing Status 1 / Status 2 picks, etc. |

### Optional: single-file installer

If the office prefers a `Setup.exe` over a zip, install
[Inno Setup 6](https://jrsoftware.org/isinfo.php), open
[`installer/enrollment.iss`](installer/enrollment.iss) after running
`make portable`, and press **Compile** — you get
`dist/EnrollmentApp-Setup.exe` that installs into Program Files with
Start-menu/desktop shortcuts and a proper uninstaller.

### Updating the app later

Change code → `make portable` again → send the new zip. Users extract
it over the old folder (or into a new one): `subjects.ini`,
`endpoint.txt`, `app.log` and `failed-submissions.jsonl` are runtime
files the packaging never ships or overwrites, so their local data and
settings survive updates.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Double-clicking the exe does nothing | GTK DLLs aren't on that PC — use the portable bundle (`make portable`) and start via `run.bat`. |
| “Windows protected your PC” on first launch | SmartScreen for unsigned exes — More info → Run anyway (once per PC). |
| `Network error (curl code 3)` | Endpoint URL malformed — re-check `endpoint.txt` / `src/config.h`. |
| `Network error (curl code -77)` or `-60` | The TLS trust store is missing — `ca-bundle.crt` must sit next to the exe (re-run `make portable`; it ships the file automatically). |
| `Network error (curl code -6/-7/-28)` | No internet, firewall, or proxy blocking `script.google.com`. |
| `HTTP 401/403` | Access isn't **Anyone**, you're using a `/dev` URL, or the app's `APP_TOKEN` doesn't match `SECRET` in Code.gs. |
| “Already submitted today…” note | Duplicate guard: this STUDENT ID (or NAME + SECTION) already has a row today. Intentional second enrolment? Set `BLOCK_DUPLICATES = false` in Code.gs. |
| App says “Not saved — the spreadsheet rejected this submission. Reason: LIMIT REACHED: …” | A subject picked ENROLLED hit the 40-per-subject hard cap on that section tab. Mark it NOT ENROLLED for this student, put the next student in another section (each section tab has its own 40 slots), or raise the cap: edit `SUBJECT_LIMIT` at the top of Code.gs, then Deploy → Manage deployments → ✏ → New version. The SUBJECT TALLY block on that section tab shows every subject's count. |
| A submission vanished (Wi-Fi died) | It's in `failed-submissions.jsonl` next to the exe — re-submit or paste into the sheet. |
| What happened on a staff PC? | Open `app.log` next to the exe — every submit, result and error is timestamped there. |
| Row landed in the wrong tab | The **Section** names the tab (`A1A`, `A1B`, …). A typo in the section (e.g. `A1B` vs `AB1`) opens a different tab — fix or delete the row by hand in the sheet and resubmit. Lowercase sections are folded onto the same tab (`a1a` → `A1A`), so case alone never splits a section. Remember: two course-years with the same section name share one tab — check the row's COURSE / YEAR LEVEL columns to tell them apart. |
| Subject column missing | Columns are created from the subject list on the next submission — submit once for that section (or run the DYCI CME APP menu fix). |
| `student_id is required` | The Student ID box was left empty — fill it in and submit again. Any text is accepted; only emptiness is rejected. |
| `Pick STATUS 1 (OLD or NEW).` / `Pick STATUS 2 (REGULAR or IRREGULAR).` | One of the two dropdowns under the Student ID box was left empty — pick both and submit again. They start empty on purpose so no wrong default is recorded. |
| App says “Saved — BUT an OLD spreadsheet script answered” (or “… is v12, expects v13”) | Your submission reached an OLD Code.gs — that old script is what ignores Student ID, re-creates DATE 2, writes subjects without labels and piles every section into one per-year tab. In the script editor: **Deploy → Manage deployments → ✏ → Version: New version → Deploy** (same URL, new code). Then submit again — the warning must disappear. If you had used **New deployment** instead, the URL changed: paste the NEW `/exec` URL into `endpoint.txt` next to the app. |
| Sheet still shows DATE 2 / `[OBJECT OBJECT]` after updating | The fixes live in Code.gs — open your `/exec` URL in a browser and read the v13 status page. It says `code v13` only if the new code is really live (otherwise: Manage deployments → ✏ → New version), shows which spreadsheet the script is bound to (a different name = pasted into the wrong project — open YOUR sheet → Extensions → Apps Script and paste it THERE, replacing the entire file), and marks every tab OK / NEEDS FIX. Then click **DYCI CME APP → Fix columns & dropdowns now** (it heals every tab, including the old per-year ones), or just submit once from the app — the submission heals the tab it writes to first, even if the student is a duplicate. |
| A `[OBJECT OBJECT]` column appeared | An older app version once posted to an older script. Run **DYCI CME APP → Fix columns & dropdowns now**: if the junk column count matches the subject list it is rescued into named subject columns (values converted), otherwise it is removed and rebuilt on the next submission. |
| Rows don't keep their highlight | Highlights live in the **HIGHLIGHT** column: the next submission or **DYCI CME APP → Fix columns & dropdowns now** re-paints every row from that column. A blank cell never changes the fill, so you can still paint rows by hand; picking NONE clears the row's fill. |
| `subjects.ini has an unknown section…` | The file was edited by hand and broke — delete it to regenerate. |
| Can't build on Linux | Missing `-dev` packages (Step 2). |
| JSON error page in browser test | Use the exact curl commands in the chat/README (Windows quoting!). |

## Ideas for later

- Sync subject lists through a `Subjects` tab instead of the local file
  (consistent across multiple PCs).
- Digitally sign the exe (code-signing certificate) to remove the
  SmartScreen warning entirely.
