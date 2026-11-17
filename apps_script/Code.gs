/**
 * Code.gs — Google Apps Script backend for the DYCI CME APP (v12).
 *
 * Setup:
 *   1. Create (or open) the Google Sheet that will store enrollments.
 *   2. Extensions -> Apps Script, delete the sample code, paste this
 *      file, and SAVE (Ctrl+S).
 *   3. Deploy -> New deployment -> type: Web app
 *        Execute as:     Me
 *        Who has access: Anyone          <-- required, or POSTs fail
 *      First time? Authorize when prompted.
 *   4. Copy the Web App URL (ends in /exec) into endpoint.txt next to
 *      the app (or src/config.h).
 *   5. Upgrading an older deployment: Deploy -> Manage deployments ->
 *      pencil icon -> Version: New version -> Deploy.
 *      IMPORTANT: "New deployment" creates a NEW URL — apps that still
 *      point at the old /exec link keep running the OLD code forever.
 *      Same deployment + "New version" = same URL, new code.
 *   6. Verify: open the /exec URL in a browser. The status page must
 *      say "code v13" AND show your spreadsheet's name plus the header
 *      health of every enrollment tab. Old text, an error, or the
 *      wrong spreadsheet name = the wrong code (or the wrong script
 *      project) is what's deployed.
 *      Also: replace the ENTIRE content of the existing Code file —
 *      do not add a second script file, or the old doPost can win.
 *      Since v7 every reply also carries "ver" — the app shows a
 *      warning right after submitting when an older script answered.
 *
 * One-click sheet repair (v5, extended in v6):
 *   Once this file is saved, the spreadsheet shows a menu
 *        "DYCI CME APP"  ->  "Fix columns & dropdowns now"
 *   Clicking it heals EVERY enrollment tab immediately — no submission
 *   needed, and this part needs no redeploy either. It fixes:
 *     - DATE 2 / UNDEFINED columns: deleted
 *     - "[OBJECT OBJECT]" subject columns (an old app/server mix-up):
 *       RESCUED — when their count matches the subject list for that
 *       tab, they are rebuilt as properly named subject columns at the
 *       end of the sheet with the old values converted to
 *       ENROLLED / NOT ENROLLED; only then are the junk columns
 *       deleted. (If the count does not match, the junk is deleted and
 *       the next submission recreates the columns.)
 *     - legacy STATUS 1 / STATUS 2 columns (the old ENROLLED / NOT
 *       ENROLLED kind): replaced by fresh ones
 *     - a STUDENT ID column is inserted after DATE if missing, and
 *       STATUS 1 / STATUS 2 are created after ENROLLMENT COMMITTEE
 *     - old checkmark-style values (TRUE, YES, Y, X, 1, ...) become
 *       ENROLLED; blank / FALSE / NO / N / 0 become NOT ENROLLED
 *     - every dropdown is re-applied (STATUS 1, STATUS 2, subjects)
 *   The same healing also runs automatically on every submission —
 *   and since v6 it runs BEFORE the row is written and BEFORE the
 *   duplicate guard, so even a duplicate submission finishes the
 *   repair of a legacy tab (DATE 2 / [OBJECT OBJECT] gone, STUDENT ID
 *   and subject columns in place, dropdowns applied).
 *
 * Behavior (v7; presentation added in v8, per-section tabs in v12,
 * section-only tab names in v13):
 *   - One TAB PER SECTION (v12). Since v13 the tab is named after the
 *     SECTION ALONE — "A1A", "A1B", "A1C", … — with no course-year
 *     prefix. Tabs are created automatically on the first submission.
 *     The course and year are still required and still fill their own
 *     columns, so no data is lost; the subject lists for a tab come
 *     from the COURSE + YEAR LEVEL values in its rows (see
 *     DEFAULT_SUBJECTS). NOTE: two course-years that use the SAME
 *     section name (e.g. BSMaRE-1 A1A and BSMT-1 A1A) share ONE tab —
 *     and with it one 40-slot quota per shared subject such as
 *     PATHFIT 2. Older layouts are not migrated and not touched:
 *     existing per-year tabs ("BSMaRE-1") and v12 tabs
 *     ("BSMaRE-1 | A1A") keep their rows; only NEW submissions open
 *     the plain section tabs.
 *   - Fixed columns: DATE, STUDENT ID, NAME, GENDER, COURSE, YEAR LEVEL,
 *     SECTION, CONTACT NUMBER, ENROLLMENT COMMITTEE, STATUS 1,
 *     STATUS 2 — then one column per subject. DATE keeps the single
 *     submission timestamp.
 *   - STUDENT ID is free-form text (no format enforced), e.g.
 *     "2026-0000" or "2026-053043".
 *   - STATUS 1 (OLD / NEW) and STATUS 2 (REGULAR / IRREGULAR) are
 *     picked on the app's form (dropdowns right below the Student ID
 *     box) and arrive filled; the sheet columns carry the same
 *     dropdowns, so staff can still correct a cell later.
 *   - EVERY subject on the app's form gets its own column (created on
 *     demand, shared by all rows of that tab). The cell holds ENROLLED
 *     or NOT ENROLLED — exactly what was picked in the app — and the
 *     whole column carries a real dropdown (data validation), so the
 *     sheet behaves like the app's interface.
 *   - Row highlights (v9): the app has a Highlight picker (NONE /
 *     YELLOW / GREEN) — the whole row gets that fill, e.g. yellow
 *     while a student's record has an error, green once it is
 *     resolved. The HIGHLIGHT column carries the pick; editing that
 *     cell in the sheet and re-running the menu fixer (or the next
 *     submission) re-paints the rows. Blank cells NEVER touch the
 *     fill, so painting rows by hand stays safe; NONE clears the
 *     fill of that row.
 *   - Subject tally + hard limit (v11; per section since v12): EVERY
 *     tab carries its own "SUBJECT TALLY" block, written to the right
 *     of the data columns (one blank column in between). It lists the
 *     tab's subjects with ENROLLED / NOT ENROLLED / SLOTS LEFT /
 *     STATUS — one independent tally per tab, rebuilt after every
 *     submission and from the menu ("Refresh subject tally").
 *     SUBJECT_LIMIT (40) is a HARD cap: when a subject the app marks
 *     ENROLLED already has that many enrollments on the tab, the
 *     submission is REJECTED and the app shows the reason — no row,
 *     no partial write. Since v13 a tab is named after the section
 *     alone (A1A, A1B, …), so each section keeps its own 40-slot
 *     quota per subject instead of sharing one across the whole year. LockService serializes submissions so two
 *     simultaneous posts cannot both slip past the limit. (v10 kept
 *     the tally on a separate tab; that tab is deleted automatically
 *     once this version runs.)
 *   - Presentation (v8): the header row is bold + horizontally
 *     CENTERED, and every data row is left-aligned + vertically
 *     centered. This runs on the WHOLE tab after every submission and
 *     from the menu fixer, so rows written earlier by an older script
 *     are tidied too. See HEADER_HALIGN / CONTENT_HALIGN below.
 *
 * Production hardening:
 *   - SECRET (optional): set a password-like string here AND in
 *     src/config.h (APP_TOKEN) and rebuild the app; POSTs without the
 *     matching token are rejected. This stops strangers who find the
 *     URL from writing into your sheet. Leave '' on both sides to
 *     disable.
 *   - BLOCK_DUPLICATES: a second row for the same student (same STUDENT
 *     ID, or — for rows without an ID — same NAME + SECTION) on the
 *     same calendar day is answered with {"status":"ok","duplicate":true}
 *     and NOT appended — the app shows an "already submitted today"
 *     note. Set to false to disable.
 *   - Every incoming field is trimmed and length-capped before it
 *     reaches the sheet.
 */

const CODE_VERSION = 'v13';
const SECRET = '';            // e.g. 'marE-2026-key'; same as APP_TOKEN in src/config.h
const BLOCK_DUPLICATES = true;
const SUBJECT_VALUES = ['ENROLLED', 'NOT ENROLLED'];
// STATUS 1 / STATUS 2 come from the app's form; the sheet columns
// carry the same dropdowns so staff can correct a cell later.
const STATUS1_VALUES = ['OLD', 'NEW'];
const STATUS2_VALUES = ['REGULAR', 'IRREGULAR'];

// Hard capacity per subject (v10; per section since v12): at most
// SUBJECT_LIMIT rows on one tab — one SECTION since v13 (e.g. "A1A") —
// may say ENROLLED for the same subject. The next submission that
// would push a subject past the limit is rejected with a clear message
// the app shows — nothing is written. Another section tab keeps its
// own quota. NOTE: because the tab is named after the section alone,
// two course-years that use the same section name share the tab and
// therefore the quota of any subject they have in common (PATHFIT 2,
// NGEC 8, ...). Entries marked NOT ENROLLED never count against the
// cap. Set 0 (or any negative number) to disable the limit entirely.
const SUBJECT_LIMIT = 40;

// The per-tab tally block (v11): rebuilt after every submission and
// from the menu ("Refresh subject tally"). It is written to the RIGHT
// of the data columns (one blank spacer column in between) on each
// tab's own sheet, so every tab keeps its own independent
// counts. FULL rows are painted red, rows down to their last
// TALLY_WARN_LEFT slots yellow — same visual language as the row
// highlights. The title cell in row 1 doubles as the MARKER that
// separates the block from the data columns (see readHeader_).
const TALLY_MARKER = 'SUBJECT TALLY';
const TALLY_HEADERS = ['SUBJECT', 'ENROLLED', 'NOT ENROLLED', 'SLOTS LEFT', 'STATUS'];
const TALLY_FULL_FILL = '#f4cccc';
const TALLY_WARN_FILL = '#fff2cc';
const TALLY_WARN_LEFT = 5;

// Presentation (v8): header row alignment + data-row alignment.
// Header = bold + HEADER_HALIGN ('center'); every data row =
// CONTENT_HALIGN ('left') + vertically centered. Change CONTENT_HALIGN
// to 'center' (or 'right') and the next submission or menu fix
// re-formats the whole tab — no other change needed.
const HEADER_HALIGN = 'center';
const CONTENT_HALIGN = 'left';

// Row highlights (v9): the HIGHLIGHT column drives the whole-row fill.
//   YELLOW / GREEN -> row painted with that color
//   NONE           -> fill removed from the row
//   blank          -> row untouched (manual fills survive)
// Add a color by adding one entry to EACH constant below (the app's
// dropdown lists NONE / YELLOW / GREEN; extend enrollment.h to match).
const HIGHLIGHT_CHOICES = ['NONE', 'YELLOW', 'GREEN'];
const HIGHLIGHT_COLORS = { 'YELLOW': '#ffff00', 'GREEN': '#b6d7a8' };

const FIXED_HEADERS = [
  'DATE', 'STUDENT ID', 'NAME', 'GENDER', 'COURSE', 'YEAR LEVEL',
  'SECTION', 'CONTACT NUMBER', 'ENROLLMENT COMMITTEE', 'STATUS 1',
  'STATUS 2', 'HIGHLIGHT'
];

// Mirror of the app's built-in subject lists (src/subjects.c), used by
// the "Fix columns & dropdowns now" menu to name rescued
// "[OBJECT OBJECT]" columns when no submission payload is available.
// Edit here if a program-year list changes in the app (subjects.ini).
const DEFAULT_SUBJECTS = {
  'BSMARE-1': ['THERMO', 'MECH', 'MACH 2', 'DRAW', 'NGEC 1', 'NGEC 4', 'PATHFIT 2'],
  'BSMARE-2': ['AUTO 2', 'AUX MACH 2', 'NAV ARCH', 'MAR ENV', 'MARLAW', 'ICT', 'PATHFIT 4'],
  'BSMARE-3': ['MAINT', 'EWATCH', 'NGEC 2', 'NGEC 6', 'NGEC 8', 'NGEC 11', 'RIZAL'],
  'BSMT-1':   ['NAV 2', 'SEAM 2', 'COLGRES', 'MET-O', 'RESEARCH (ELECTIVE)', 'PATHFIT 2'],
  'BSMT-2':   ['NAV 1', 'NAV 5', 'SEAM 4', 'MARCOM', 'MGMT', 'NGEC 8', 'PATHFIT 4'],
  'BSMT-3':   ['DECK WATCH', 'NAV 7', 'SEAM 6', 'NGEC 2', 'NGEC 6', 'NGEC 11', 'RIZAL']
};

// Legacy checkmark values that mean "enrolled" / "not enrolled" in old
// columns. Anything else in a subject column is left untouched.
const LEGACY_ENROLLED = { '✓': 1, '✔': 1, 'TRUE': 1, 'YES': 1, 'Y': 1, 'X': 1, '1': 1 };
const LEGACY_NOT_ENROLLED = { 'FALSE': 1, 'NO': 1, 'N': 1, '0': 1 };

function doPost(e) {
  let d;
  try {
    d = JSON.parse(e.postData.contents);
  } catch (err) {
    return reply({ status: 'error', message: 'bad JSON: ' + String(err) });
  }

  // One submission at a time (v10): the per-subject hard limit reads
  // a count and then appends. Two simultaneous posts must never both
  // see "39 ENROLLED" and both append — a script lock makes the
  // limit truly hard. waitLock only throws on a 30-second timeout;
  // in that rare case the submission still runs, just without the
  // guarantee, rather than losing the row.
  const lock = LockService.getScriptLock();
  let locked = false;
  try {
    lock.waitLock(30000);
    locked = true;
  } catch (e2) { /* proceed unlocked rather than lose the row */ }
  try {
    return handleSubmit_(d);
  } finally {
    if (locked) {
      try { lock.releaseLock(); } catch (e3) { /* cosmetic */ }
    }
  }
}

function handleSubmit_(d) {
  try {

    if (SECRET && String(d.token || '') !== SECRET) {
      return reply({ status: 'error', message: 'bad token' });
    }

    // Basic sanity + safe tab name (letters/digits/hyphen only).
    const course = sanitizeToken(d.course);
    const year = sanitizeToken(d.year_level);
    if (!course || !year) {
      return reply({ status: 'error', message: 'course or year_level missing' });
    }

    // v13: one tab per SECTION, named after the section ALONE — "A1A",
    // "A1B", … — instead of "BSMaRE-1 | A1A" (v12) or "BSMaRE-1" (v11
    // and earlier). The 40-slot quota applies to each section on its
    // own. Course and year stay required: they fill their own columns
    // on the row and pick the tab's subject lists (DEFAULT_SUBJECTS is
    // looked up from the COURSE + YEAR LEVEL values in the rows, since
    // the name no longer carries them).
    const sectionTab = sectionTabPart_(d.section);
    if (!sectionTab) {
      return reply({ status: 'error', message: 'section missing' });
    }
    const tabName = sectionTab;

    // STUDENT ID: required, free-form text — no format is enforced
    // (e.g. 2026-0000, 2026-053043, or any office-specific scheme).
    // Upper-cased so the duplicate guard matches IDs regardless of case.
    const studentId = clip_(String(d.student_id || ''), 64)
                        .trim().toUpperCase();
    if (!studentId) {
      return reply({ status: 'error', message: 'student_id is required' });
    }

    const name = clip_(d.name, 120);
    const section = clip_(d.section, 40);
    if (!name || !section) {
      return reply({ status: 'error', message: 'name or section missing' });
    }

    // STATUS 1 / STATUS 2: picked on the app's form (dropdowns under
    // the Student ID box). Unknown or missing values — e.g. an older
    // app version that does not send them — stay blank so staff can
    // fill the cell via the sheet dropdown instead.
    let status1 = clip_(d.status1, 16).trim().toUpperCase();
    if (STATUS1_VALUES.indexOf(status1) === -1) status1 = '';
    let status2 = clip_(d.status2, 16).trim().toUpperCase();
    if (STATUS2_VALUES.indexOf(status2) === -1) status2 = '';

    // HIGHLIGHT (v9): optional whole-row fill picked in the app —
    // NONE / YELLOW / GREEN. Unknown or missing values stay blank,
    // which leaves any fill already in the sheet untouched.
    let highlight = clip_(d.highlight, 12).trim().toUpperCase();
    if (HIGHLIGHT_CHOICES.indexOf(highlight) === -1) highlight = '';

    // Subjects: the app sends EVERY subject with its status, e.g.
    // [{"name":"NAV 2","status":"ENROLLED"},...]. Older apps (v2/v3)
    // sent plain strings of the ENROLLED ones — still accepted.
    const subjects = [];
    (Array.isArray(d.subjects) ? d.subjects : []).forEach(function (s) {
      let nm, st;
      if (s && typeof s === 'object') {
        if (typeof s.name !== 'string') return; // malformed entry, skip
        nm = clip_(s.name, 64);
        st = String(s.status || '').trim().toUpperCase();
      } else {
        nm = clip_(s, 64);
        st = 'ENROLLED';
      }
      if (!nm) return;
      // Never let a junk label become a column (an old app/server
      // version mix-up once created "[OBJECT OBJECT]" headers).
      if (/^\[object/i.test(nm) || /^undefined$/i.test(nm)) return;
      if (SUBJECT_VALUES.indexOf(st) === -1) st = '';
      subjects.push({ name: nm, status: st });
    });

    // Uppercase, de-duplicated subject names, in form order — used to
    // heal the tab and (if a junk column rescue is possible) to rename
    // the "[OBJECT OBJECT]" columns.
    const subjectNames = [];
    const seen = {};
    subjects.forEach(function (s) {
      const key = s.name.toUpperCase();
      if (!seen[key]) {
        seen[key] = 1;
        subjectNames.push(key);
      }
    });

    const ss = SpreadsheetApp.getActiveSpreadsheet();
    let sheet = ss.getSheetByName(tabName);
    if (!sheet) {
      sheet = ss.insertSheet(tabName);
      sheet.getRange(1, 1, 1, FIXED_HEADERS.length).setValues([FIXED_HEADERS]);
      sheet.getRange(1, 1, 1, FIXED_HEADERS.length).setFontWeight('bold');
    } else {
      healTab_(sheet, subjectNames); // v3/v4/v5 -> v6 layout, idempotent
    }

    // Read the current header row and index it (case-insensitive).
    // readHeader_ stops at the tally block's marker column, so the
    // block's own headers are never mistaken for data columns (v11).
    const header = readHeader_(sheet);
    const prevWidth = header.length;
    const idx = {};
    header.forEach(function (h, i) {
      idx[h.trim().toUpperCase()] = i;
    });

    // Make sure every fixed column exists.
    FIXED_HEADERS.forEach(function (h) {
      if (!(h in idx)) {
        header.push(h);
        idx[h] = header.length - 1;
      }
    });

    // One column per subject — created BEFORE the duplicate guard so a
    // submission fully repairs the tab even when the row itself turns
    // out to be a duplicate (v6).
    subjects.forEach(function (sub) {
      const key = sub.name.toUpperCase();
      if (!(key in idx)) {
        header.push(key);
        idx[key] = header.length - 1;
      }
    });

    // Persist the wider header row now (no-op once up to date). Only
    // the data columns are written — the tally block sits further right.
    if (header.length > prevWidth) {
      sheet.getRange(1, 1, 1, header.length).setValues([header]);
      sheet.getRange(1, 1, 1, header.length).setFontWeight('bold');
    }

    // Re-apply every dropdown (STATUS 1, STATUS 2 and all subject
    // columns) BEFORE the duplicate guard, so duplicate submissions
    // also finish repairing the tab. The range starts at row 2 and is
    // at least 200 rows tall, so the row appended below is covered too.
    const subjectKeys = {};
    subjectNames.forEach(function (n) { subjectKeys[n] = true; });
    applyDropdowns_(sheet, header, subjectKeys);

    const stamp = d.submitted_at ? clip_(d.submitted_at, 32)
                                 : new Date().toISOString();

    // Duplicate guard: same student already logged today? (By now the
    // tab has already been healed, its subject columns created and its
    // dropdowns applied — the guard only skips the ROW below.)
    if (BLOCK_DUPLICATES &&
        isDuplicate_(sheet, idx, studentId, name, section, stamp)) {
      formatTab_(sheet); // the presentation fix also survives the guard
      return reply({ status: 'ok', duplicate: true, tab: tabName });
    }

    // Hard limit (v10): reject the submission when a subject the app
    // marked ENROLLED is already at SUBJECT_LIMIT on this tab. Nothing
    // is written — the app shows the reason to the committee. The
    // count is per TAB — one section since v13 — so another section
    // tab keeps its own quota.
    if (SUBJECT_LIMIT > 0) {
      const counts = countSubjectStatuses_(sheet, header);
      const full = [];
      subjects.forEach(function (sub) {
        if (sub.status !== 'ENROLLED') return;
        const key = sub.name.toUpperCase();
        const have = counts[key] ? counts[key].enrolled : 0;
        if (have >= SUBJECT_LIMIT) {
          full.push(key + ' is full (' + have + '/' + SUBJECT_LIMIT +
                    ' ENROLLED)');
        }
      });
      if (full.length) {
        return reply({
          status: 'error',
          message: 'LIMIT REACHED: ' + full.join(', ') + ' on ' + tabName +
                   '. No row was added. Set that subject to NOT ENROLLED ' +
                   'or ask the committee.',
          full: full
        });
      }
    }

    const row = new Array(header.length).fill('');
    row[idx['DATE']] = stamp;
    row[idx['STUDENT ID']] = studentId;
    row[idx['NAME']] = name;
    row[idx['GENDER']] = clip_(d.gender, 8);
    row[idx['COURSE']] = clip_(d.course, 20);
    row[idx['YEAR LEVEL']] = clip_(d.year_level, 4);
    row[idx['SECTION']] = section;
    row[idx['CONTACT NUMBER']] = clip_(d.contact_number, 32);
    row[idx['ENROLLMENT COMMITTEE']] = clip_(d.committee, 60);
    row[idx['STATUS 1']] = status1; // OLD / NEW from the app
    row[idx['STATUS 2']] = status2; // REGULAR / IRREGULAR from the app
    row[idx['HIGHLIGHT']] = highlight; // NONE / YELLOW / GREEN

    // Subject cells: ENROLLED / NOT ENROLLED, exactly as picked.
    subjects.forEach(function (sub) {
      row[idx[sub.name.toUpperCase()]] = sub.status;
    });

    // v11: the row is written explicitly BELOW the last data row —
    // sheet.appendRow() would land UNDER the tally block, because the
    // block's rows extend the sheet's used range to the right.
    const at = lastDataRow_(sheet, header.length) + 1;
    sheet.getRange(at, 1, 1, header.length).setValues([row]);
    formatTab_(sheet); // header centered, data rows left-aligned (v8)

    // Keep this tab's tally block in step with the new row (v11), and
    // retire the v10 tally tab if one is still around. Best effort —
    // a tally problem must never fail a submission.
    try { removeLegacyTallyTab_(); } catch (e) { /* cosmetic */ }
    try { writeTallyBlock_(sheet, header); } catch (e) { /* cosmetic */ }

    return reply({ status: 'ok', tab: tabName });
  } catch (err) {
    return reply({ status: 'error', message: String(err) });
  }
}

/**
 * Status page for checking the deployment in a browser (v6, kept
 * since). Answers:
 *   1. WHICH code is live — after a redeploy it must say "code v13".
 *   2. WHICH spreadsheet this script writes to (name + URL) — if that
 *      is not the sheet you are checking, the script was pasted into
 *      a different project than the one bound to your sheet.
 *   3. The header health of every enrollment tab — DATE 2 or
 *      [OBJECT OBJECT] still present, missing STUDENT ID / STATUS /
 *      subject columns, etc. Read-only: this page never writes.
 */
function doGet() {
  const out = [];
  out.push('DYCI CME APP enrollment endpoint is live — code ' + CODE_VERSION + '.');
  out.push('');

  let ss = null;
  try { ss = SpreadsheetApp.getActiveSpreadsheet(); } catch (err) { ss = null; }
  if (!ss) {
    out.push('PROBLEM: this script is NOT bound to a spreadsheet, so there is');
    out.push('nowhere to write enrollments. Open your Google Sheet, choose');
    out.push('Extensions -> Apps Script and paste this code THERE, replacing');
    out.push('the entire content of the existing file, then deploy it again.');
    return textOutput_(out.join('\n'));
  }

  out.push('Bound spreadsheet: ' + ss.getName());
  try { out.push('Spreadsheet URL: ' + ss.getUrl()); } catch (e) { /* cosmetic */ }
  out.push('');

  const tabs = [];
  ss.getSheets().forEach(function (sheet) {
    const lastCol = sheet.getLastColumn();
    if (lastCol <= 0) return;
    const header = sheet.getRange(1, 1, 1, lastCol).getValues()[0]
      .map(function (h) { return String(h).trim().toUpperCase(); });
    if (header.indexOf('DATE') === -1 || header.indexOf('NAME') === -1) {
      return; // not an enrollment tab
    }
    tabs.push({ sheet: sheet, name: sheet.getName(), header: header });
  });

  if (!tabs.length) {
    out.push('No enrollment tabs yet. Tabs are created automatically on the');
    out.push('first submission, named after the section (e.g. "A1A").');
    return textOutput_(out.join('\n'));
  }

  out.push('Enrollment tabs and their current header health:');
  tabs.forEach(function (t) {
    const problems = tabProblems_(t.sheet, t.header);
    out.push('');
    out.push('  ' + t.name + (problems.length ? ' — NEEDS FIX' : ' — OK'));
    if (problems.length) {
      problems.forEach(function (p) { out.push('     * ' + p); });
      out.push('     Fix: menu "DYCI CME APP -> Fix columns & dropdowns now",');
      out.push('     or just submit once from the app — the submission heals');
      out.push('     the tab first, then the row is written.');
    }
  });
  out.push('');
  out.push('Subject limit: ' + (SUBJECT_LIMIT > 0
    ? SUBJECT_LIMIT + ' ENROLLED per subject, per section tab ' +
      '(tabs are named after the section, e.g. "A1A").'
    : 'disabled (SUBJECT_LIMIT <= 0 in Code.gs).'));
  out.push('Every tab carries its own "' + TALLY_MARKER + '" block right');
  out.push('of the data columns: per-subject ENROLLED, NOT ENROLLED, SLOTS');
  out.push('LEFT and FULL/OPEN status — independent counts per tab.');
  out.push('');
  out.push('If a tab you are checking is missing from this list, this script is');
  out.push('bound to a different spreadsheet than the one you are looking at.');
  return textOutput_(out.join('\n'));
}

function textOutput_(s) {
  return ContentService.createTextOutput(s)
    .setMimeType(ContentService.MimeType.TEXT);
}

/** What is still wrong with one tab's header, for the status page. */
function tabProblems_(sheet, header) {
  const problems = [];
  if (header.indexOf('DATE 2') !== -1) {
    problems.push('still has a DATE 2 column');
  }
  const junk = header.filter(function (h) {
    return h.indexOf('[OBJECT') === 0 || h === 'UNDEFINED';
  });
  if (junk.length) {
    problems.push('still has ' + junk.length + ' [OBJECT OBJECT]/UNDEFINED column(s)');
  }
  if (header.indexOf('STUDENT ID') === -1) {
    problems.push('STUDENT ID column missing');
  }
  if (header.indexOf('HIGHLIGHT') === -1) {
    problems.push('HIGHLIGHT column missing');
  }
  ['STATUS 1', 'STATUS 2'].forEach(function (h) {
    if (header.indexOf(h) === -1) problems.push(h + ' column missing');
  });
  const want = defaultSubjectsForTab_(sheet) || [];
  if (want) {
    const have = want.filter(function (n) { return header.indexOf(n) !== -1; });
    if (have.length < want.length) {
      problems.push('only ' + have.length + ' of ' + want.length +
                    ' subject columns present' +
                    (have.length ? ' (' + have.join(', ') + ')' : ''));
    }
  }
  return problems;
}

/** The spreadsheet menu. Appears once this file is saved in the script
 *  editor (reload the spreadsheet tab if you don't see it yet). */
function onOpen() {
  SpreadsheetApp.getUi()
    .createMenu('DYCI CME APP')
    .addItem('Fix columns & dropdowns now', 'fixEnrollmentSheets')
    .addItem('Refresh subject tally', 'refreshSubjectTallies')
    .addToUi();
}

/**
 * Menu action: heal every enrollment tab in this spreadsheet right now
 * (no submission needed). Enrollment tabs are recognized by having
 * DATE and NAME in row 1. Shows a summary toast when done.
 */
function fixEnrollmentSheets() {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  const report = [];

  ss.getSheets().forEach(function (sheet) {
    const lastCol = sheet.getLastColumn();
    if (lastCol <= 0) return;
    const header = sheet.getRange(1, 1, 1, lastCol).getValues()[0]
      .map(function (h) { return String(h).trim().toUpperCase(); });
    if (header.indexOf('DATE') === -1 || header.indexOf('NAME') === -1) {
      return; // not an enrollment tab (since v13 tabs are named after the section)
    }
    const subjectNames = defaultSubjectsForTab_(sheet);
    report.push(sheet.getName() + ': ' + healTab_(sheet, subjectNames));
  });

  // v11: the tally block on every enrollment tab is derived from the
  // data — refresh them all in the same click so one menu action
  // leaves the whole spreadsheet current.
  try {
    report.push(refreshSubjectTallies());
  } catch (e) {
    report.push('subject tally refresh failed: ' + String(e));
  }

  const msg = report.length
    ? report.join('\n')
    : 'No enrollment tabs found — tabs are named after the section ' +
      '(e.g. "A1A").';
  Logger.log(msg);
  try {
    ss.toast(msg, 'DYCI CME APP — sheet fix', 10);
  } catch (e) { /* toast is cosmetic */ }
  return msg;
}

/**
 * Bring one tab to the v5 layout. Idempotent — safe to run any number
 * of times. `subjectNames` (uppercase, in order) is the authoritative
 * subject list for this tab: the submission payload when called from
 * doPost, or the built-in default list when called from the menu.
 * Returns a one-line summary of what changed.
 */
function healTab_(sheet, subjectNames) {
  let lastCol = sheet.getLastColumn();
  if (lastCol <= 0) return 'empty tab, nothing to fix';

  let header = readHeader_(sheet);
  const lastRow = sheet.getLastRow();
  const notes = [];

  // ---- 1. Columns to drop outright: DATE 2, UNDEFINED, and legacy
  //         STATUS 1 / STATUS 2 (the old ENROLLED / NOT ENROLLED kind).
  const drop = [];
  for (let c = 0; c < header.length; c++) {
    const h = header[c];
    if (h === 'DATE 2' || h === 'UNDEFINED') {
      drop.push(c);
    } else if (h === 'STATUS 1' || h === 'STATUS 2') {
      // Legacy kind? The old columns hold ENROLLED / NOT ENROLLED —
      // anything else non-blank is not one of the new values either.
      const allowed = h === 'STATUS 1' ? STATUS1_VALUES : STATUS2_VALUES;
      if (lastRow > 1) {
        const vals = sheet.getRange(2, c + 1, lastRow - 1, 1).getValues();
        for (let r = 0; r < vals.length; r++) {
          const v = String(vals[r][0]).trim().toUpperCase();
          if (v !== '' && allowed.indexOf(v) === -1) {
            drop.push(c);
            notes.push('replaced legacy ' + h);
            break;
          }
        }
      }
    }
  }
  for (let d = drop.length - 1; d >= 0; d--) {
    sheet.deleteColumns(drop[d] + 1, 1);
  }
  if (drop.length) {
    notes.push('deleted ' + drop.length + ' old col(s) (e.g. DATE 2)');
  }

  // ---- 2. STUDENT ID right after DATE if missing.
  header = readHeader_(sheet);
  if (header.indexOf('STUDENT ID') === -1) {
    const datePos = header.indexOf('DATE');
    const at = datePos === -1 ? 1 : datePos + 2; // 1-based insert position
    sheet.insertColumns(at);
    sheet.getRange(1, at).setValue('STUDENT ID');
    sheet.getRange(1, at).setFontWeight('bold');
  }

  // ---- 3. STATUS 1 / STATUS 2 right after ENROLLMENT COMMITTEE.
  header = readHeader_(sheet);
  const committeePos = header.indexOf('ENROLLMENT COMMITTEE');
  if (committeePos !== -1) {
    let at = committeePos + 2; // 1-based: first column right of committee
    if (header.indexOf('STATUS 1') === -1) {
      sheet.insertColumns(at);
      sheet.getRange(1, at).setValue('STATUS 1');
      sheet.getRange(1, at).setFontWeight('bold');
      header.splice(at - 1, 0, 'STATUS 1');
    }
    if (header[committeePos + 1] === 'STATUS 1') at++; // keep 1 then 2
    if (header.indexOf('STATUS 2') === -1) {
      sheet.insertColumns(at);
      sheet.getRange(1, at).setValue('STATUS 2');
      sheet.getRange(1, at).setFontWeight('bold');
    }
  }

  // ---- 3b. HIGHLIGHT right after STATUS 2 (v9).
  header = readHeader_(sheet);
  if (header.indexOf('HIGHLIGHT') === -1) {
    let anchor = header.indexOf('STATUS 2');
    if (anchor === -1) anchor = header.indexOf('STATUS 1');
    if (anchor === -1) anchor = header.indexOf('ENROLLMENT COMMITTEE');
    const at = anchor === -1 ? header.length + 1 : anchor + 2; // 1-based
    sheet.insertColumns(at);
    sheet.getRange(1, at).setValue('HIGHLIGHT');
    sheet.getRange(1, at).setFontWeight('bold');
  }

  // ---- 4. Rescue "[OBJECT OBJECT]" subject columns: copy their values
  //         into properly named columns at the end of the sheet, then
  //         delete the junk. Only when the junk count matches the
  //         subject list and no proper subject column exists yet.
  header = readHeader_(sheet);
  const rescue = rescueJunkSubjects_(sheet, header, subjectNames);
  if (rescue.rescued > 0) {
    notes.push('rescued ' + rescue.rescued + ' [OBJECT OBJECT] col(s) into named subject columns');
  } else if (rescue.junkDeleted > 0) {
    notes.push('deleted ' + rescue.junkDeleted + ' [OBJECT OBJECT] col(s) (count did not match — columns return on the next submission)');
  }

  // ---- 5. Convert legacy marks in every subject-like column.
  header = readHeader_(sheet);
  const treated = normalizeSubjectColumns_(sheet, header);
  if (treated.length) {
    notes.push('subject values set to ENROLLED / NOT ENROLLED in ' +
               treated.length + ' col(s)');
  }

  // ---- 6. Re-apply every dropdown.
  header = readHeader_(sheet);
  const subjectKeys = {};
  (subjectNames || []).forEach(function (n) { subjectKeys[n] = true; });
  treated.forEach(function (n) { subjectKeys[n] = true; });
  applyDropdowns_(sheet, header, subjectKeys);

  // ---- 7. Presentation (v8): bold centered header, left-aligned rows
  //         — also tidies rows written earlier by an older script.
  formatTab_(sheet);

  return notes.length ? notes.join('; ') : 'already up to date';
}

/** Row 1, trimmed + uppercased — the canonical header view. Stops at
 *  the tally block (v11): everything from the "SUBJECT TALLY" marker
 *  rightward is the block, not data columns; blank columns right before
 *  the marker are the block's spacer and are dropped too, so the
 *  header length is always the true data width. */
function readHeader_(sheet) {
  const lastCol = sheet.getLastColumn();
  if (lastCol <= 0) return [];
  const all = sheet.getRange(1, 1, 1, lastCol).getValues()[0]
    .map(function (h) { return String(h).trim().toUpperCase(); });
  for (let c = 0; c < all.length; c++) {
    if (all[c].indexOf(TALLY_MARKER) === 0) {
      let end = c;
      while (end > 0 && all[end - 1] === '') end--; // drop the spacer
      return all.slice(0, end);
    }
  }
  return all;
}

/**
 * Classify one cell of a subject column:
 *   'E' = enrolled-ish, 'N' = not-enrolled-ish, '' = blank,
 *   null = something else (column is then NOT treated as a subject).
 */
function subjectCellClass_(v) {
  const s = String(v == null ? '' : v).trim().toUpperCase();
  if (s === '') return '';
  if (LEGACY_ENROLLED[s] || s === 'ENROLLED') return 'E';
  if (LEGACY_NOT_ENROLLED[s] || s === 'NOT ENROLLED') return 'N';
  return null;
}

/**
 * The junk-column rescue. If every "[OBJECT OBJECT]" column can be
 * matched to a subject (same count, no proper subject column exists,
 * all values are subject-like), new named subject columns are appended
 * after the last column, the legacy values are copied over as
 * ENROLLED / NOT ENROLLED, and the junk columns are removed. Returns
 * { rescued, junkDeleted }.
 */
function rescueJunkSubjects_(sheet, header, subjectNames) {
  const junkIdx = [];
  header.forEach(function (h, c) {
    if (h.indexOf('[OBJECT') === 0) junkIdx.push(c);
  });
  if (junkIdx.length === 0) return { rescued: 0, junkDeleted: 0 };

  // Data rows only (v11): the tally block's rows share sheet rows with
  // the students and must not receive converted values.
  const dataLast = lastDataRow_(sheet, header.length);

  const ok = subjectNames && subjectNames.length === junkIdx.length;
  if (ok) {
    for (let i = 0; i < subjectNames.length; i++) {
      if (header.indexOf(subjectNames[i]) !== -1) { ok = false; break; }
    }
  }
  if (ok) {
    for (let j = 0; j < junkIdx.length && ok; j++) {
      if (dataLast > 1) {
        const vals = sheet.getRange(2, junkIdx[j] + 1, dataLast - 1, 1).getValues();
        for (let r = 0; r < vals.length; r++) {
          if (subjectCellClass_(vals[r][0]) === null) { ok = false; break; }
        }
      }
    }
  }

  let rescued = 0;
  if (ok) {
    const startAt = header.length + 1; // 1-based append position
    sheet.insertColumns(startAt, subjectNames.length);
    for (let i = 0; i < subjectNames.length; i++) {
      sheet.getRange(1, startAt + i).setValue(subjectNames[i]);
      sheet.getRange(1, startAt + i).setFontWeight('bold');
    }
    if (dataLast > 1) {
      const src = sheet.getRange(2, junkIdx[0] + 1, dataLast - 1,
                                 junkIdx.length).getValues();
      const out = src.map(function (row) {
        return row.map(function (v) {
          return subjectCellClass_(v) === 'E' ? 'ENROLLED' : 'NOT ENROLLED';
        });
      });
      sheet.getRange(2, startAt, dataLast - 1, junkIdx.length).setValues(out);
    }
    rescued = junkIdx.length;
  }

  // Junk never survives: delete right-to-left so indices stay valid.
  for (let d = junkIdx.length - 1; d >= 0; d--) {
    sheet.deleteColumns(junkIdx[d] + 1, 1);
  }
  return { rescued: rescued, junkDeleted: junkIdx.length };
}

/**
 * Convert legacy marks in every subject-like column to the canonical
 * ENROLLED / NOT ENROLLED text:
 *   TRUE / YES / Y / X / 1 / ✓ / ENROLLED      -> ENROLLED
 *   blank / FALSE / NO / N / 0 / NOT ENROLLED  -> NOT ENROLLED
 * Columns holding anything else (free text, dates, ...) are left
 * untouched. Returns the uppercase names of the columns treated.
 */
function normalizeSubjectColumns_(sheet, header) {
  const fixed = {};
  FIXED_HEADERS.forEach(function (h) { fixed[h] = true; });

  const treated = [];
  // Only DATA rows are converted (v11): the tally block's rows share
  // sheet rows with the students, and their blank cells in the subject
  // columns must stay blank instead of becoming 'NOT ENROLLED'.
  const dataLast = lastDataRow_(sheet, header.length);
  if (dataLast <= 1) return treated;

  header.forEach(function (h, c) {
    const key = String(h).trim().toUpperCase();
    if (!key || fixed[key]) return;

    const vals = sheet.getRange(2, c + 1, dataLast - 1, 1).getValues();
    for (let r = 0; r < vals.length; r++) {
      if (subjectCellClass_(vals[r][0]) === null) return; // not a subject column
    }

    let changed = false;
    const out = vals.map(function (row) {
      const v = subjectCellClass_(row[0]) === 'E' ? 'ENROLLED' : 'NOT ENROLLED';
      if (String(row[0]).trim().toUpperCase() !== v) changed = true;
      return [v];
    });
    if (changed) {
      sheet.getRange(2, c + 1, dataLast - 1, 1).setValues(out);
    }
    treated.push(key);
  });
  return treated;
}

/**
 * Re-apply dropdowns on the data rows — covers rows added later:
 *   - STATUS 1 → OLD / NEW
 *   - STATUS 2 → REGULAR / IRREGULAR
 *   - columns named in `subjectKeys` → ENROLLED / NOT ENROLLED
 * (Apps Script writes ignore validation, so the app's own values are
 * never blocked; the dropdowns guard manual edits in the sheet.)
 */
function applyDropdowns_(sheet, header, subjectKeys) {
  subjectKeys = subjectKeys || {};
  const fixed = {};
  FIXED_HEADERS.forEach(function (h) { fixed[h] = true; });

  function rule(values) {
    return SpreadsheetApp.newDataValidation()
      .requireValueInList(values, true)
      .setAllowInvalid(false)
      .setHelpText('Pick a value from the list')
      .build();
  }
  const statusRules = {
    'STATUS 1': rule(STATUS1_VALUES),
    'STATUS 2': rule(STATUS2_VALUES),
    'HIGHLIGHT': rule(HIGHLIGHT_CHOICES)
  };

  const rows = Math.max(200, sheet.getLastRow()); // cover future rows too
  header.forEach(function (h, i) {
    const key = String(h).trim().toUpperCase();
    const r = statusRules[key] ||
              (subjectKeys[key] && !fixed[key] ? rule(SUBJECT_VALUES) : null);
    if (r) sheet.getRange(2, i + 1, rows, 1).setDataValidation(r);
  });
}

/**
 * Presentation (v8): the header row is bold + horizontally centered
 * and every data row is left-aligned + vertically centered. Idempotent
 * — it runs after every submission and from the menu fixer, so it also
 * tidies up rows written earlier by an older script.
 */
function formatTab_(sheet) {
  const lastCol = sheet.getLastColumn();
  if (lastCol <= 0) return;
  const header = readHeader_(sheet); // data columns only (v11)
  const dataWidth = Math.max(1, header.length);

  const hdr = sheet.getRange(1, 1, 1, lastCol);
  hdr.setFontWeight('bold');
  hdr.setHorizontalAlignment(HEADER_HALIGN);
  hdr.setVerticalAlignment('middle');

  const lastRow = sheet.getLastRow();
  if (lastRow > 1) {
    // Body styling stays within the data columns (v11) so the tally
    // block to the right keeps its own alignment.
    const body = sheet.getRange(2, 1, lastRow - 1, dataWidth);
    body.setHorizontalAlignment(CONTENT_HALIGN);
    body.setVerticalAlignment('middle');

    // Row fills (v9): the HIGHLIGHT column drives the paint.
    // YELLOW / GREEN paint the row, NONE clears it, and blank cells
    // are skipped so manual fills survive.
    const hlAt = header.indexOf('HIGHLIGHT');
    if (hlAt !== -1) {
      const marks = sheet.getRange(2, hlAt + 1, lastRow - 1, 1).getValues();
      for (let r = 0; r < marks.length; r++) {
        const v = String(marks[r][0]).trim().toUpperCase();
        if (v === '') continue; // untouched row
        sheet.getRange(r + 2, 1, 1, dataWidth)
          .setBackground(HIGHLIGHT_COLORS[v] || null); // NONE -> clear
      }
    }
  }
}

/**
 * Per-subject counts on one tab (v10): for every non-fixed column,
 * how many data cells hold ENROLLED-ish / NOT ENROLLED-ish values
 * (subjectCellClass_ also recognizes the legacy marks). Cells holding
 * anything else simply count as neither, so a stray hand-typed value
 * can never hide a real enrollment from the limit check. Returns
 * { 'SUBJECT': { enrolled, notEnrolled, total }, ... }.
 */
function countSubjectStatuses_(sheet, header) {
  const counts = {};
  const fixed = {};
  FIXED_HEADERS.forEach(function (h) { fixed[h] = true; });

  const lastRow = sheet.getLastRow();
  if (lastRow <= 1) return counts;
  const width = Math.max(1, sheet.getLastColumn());
  const rows = sheet.getRange(2, 1, lastRow - 1, width).getValues();

  header.forEach(function (h, c) {
    const key = String(h).trim().toUpperCase();
    if (!key || fixed[key] || c >= width) return;
    let e = 0;
    let n = 0;
    for (let r = 0; r < rows.length; r++) {
      const cls = subjectCellClass_(rows[r][c]);
      if (cls === 'E') e++;
      else if (cls === 'N') n++;
    }
    counts[key] = { enrolled: e, notEnrolled: n, total: e + n };
  });
  return counts;
}

/**
 * Menu action (v11): rebuild the "SUBJECT TALLY" block on EVERY
 * enrollment tab (no submission needed), and delete the old v10
 * "SUBJECT TALLY" tab if one is still around. Returns a summary line.
 */
function refreshSubjectTallies() {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  const gone = removeLegacyTallyTab_(ss);

  const report = [];
  ss.getSheets().forEach(function (sheet) {
    const header = readHeader_(sheet); // data columns only
    if (header.indexOf('DATE') === -1 || header.indexOf('NAME') === -1) {
      return; // not an enrollment tab
    }
    try {
      const line = writeTallyBlock_(sheet, header);
      report.push(sheet.getName() + ': ' + (line || 'no subjects to tally'));
    } catch (e) {
      report.push(sheet.getName() + ': tally failed — ' + String(e));
    }
  });

  const msg = (gone ? gone + '\n' : '') +
    (report.length
      ? report.join('\n')
      : 'No enrollment tabs yet — submit once from the app first.');
  Logger.log(msg);
  try { ss.toast(msg, 'DYCI CME APP — subject tally', 10); } catch (e) {}
  return msg;
}

/** Delete the v10 tally tab, if this spreadsheet still has one (v11
 *  migration). The tally now lives on each enrollment tab itself. Safe
 *  to call any number of times; a user-owned tab of that name is
 *  treated as ours and removed too — it only ever existed for the old
 *  tally. */
function removeLegacyTallyTab_() {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  const old = ss.getSheetByName('SUBJECT TALLY');
  if (!old) return '';
  try { ss.deleteSheet(old); } catch (e) { return ''; }
  return 'removed the old "' + old.getName() + '" tab (the tally now lives on each enrollment tab)';
}

/**
 * The per-tab tally block (v11): written to the right of the data
 * columns on `sheet` — row 1 holds the "SUBJECT TALLY …" title (which
 * doubles as the marker readHeader_ stops at), row 2 the column
 * headers, and below that one row per subject with ENROLLED / NOT
 * ENROLLED / SLOTS LEFT / STATUS. Rebuilt from scratch on every call
 * so the numbers always match the data. FULL rows are painted red,
 * rows down to their last few slots yellow. Returns a summary line.
 */
function writeTallyBlock_(sheet, header) {
  const counts = countSubjectStatuses_(sheet, header);
  // The built-in subject lists (DEFAULT_SUBJECTS) are keyed by
  // "COURSE-YEAR" (v13): the keys are read from the tab's COURSE +
  // YEAR LEVEL data columns, since the tab name no longer carries the
  // course-year — a tab named "A1A" tallies whatever course-years its
  // rows belong to. The matched lists come first so the tally mirrors
  // the app's form even where a subject has no column yet; extra
  // subject-like columns that hold entries follow, alphabetically.
  const names = defaultSubjectsForTab_(sheet) || [];
  Object.keys(counts).sort().forEach(function (n) {
    if (n.indexOf('[OBJECT') === 0 || n === 'UNDEFINED' || n === 'DATE 2') return;
    if (names.indexOf(n) !== -1) return;
    if (counts[n].total > 0) names.push(n);
  });
  if (!names.length) return '';

  const width = TALLY_HEADERS.length; // 5
  const capped = SUBJECT_LIMIT > 0;
  const values = [[TALLY_MARKER + (capped
    ? ' — max ' + SUBJECT_LIMIT + ' ENROLLED per subject'
    : ' — no limit'), '', '', '', ''],
    TALLY_HEADERS];
  const paints = [];
  let fullCount = 0;
  names.forEach(function (n) {
    const c = counts[n] || { enrolled: 0, notEnrolled: 0, total: 0 };
    const left = capped ? Math.max(0, SUBJECT_LIMIT - c.enrolled) : '';
    const status = capped ? (c.enrolled >= SUBJECT_LIMIT ? 'FULL' : 'OPEN')
                          : 'NO LIMIT';
    values.push([n, c.enrolled, c.notEnrolled, left, status]);
    if (status === 'FULL') { paints.push({ bg: TALLY_FULL_FILL }); fullCount++; }
    else if (capped && typeof left === 'number' && left <= TALLY_WARN_LEFT) {
      paints.push({ bg: TALLY_WARN_FILL });
    } else {
      paints.push({ bg: null }); // open with room: never painted
    }
  });

  // The block MOVES when the data grows a column (e.g. a new subject):
  // everything from the old marker rightward is script-owned, so it is
  // deleted wholesale before the block is written at its new spot.
  const lastCol = sheet.getLastColumn();
  const start = header.length + 2; // one blank spacer column
  if (lastCol > header.length) {
    const row1 = sheet.getRange(1, header.length + 1, 1, lastCol - header.length)
      .getValues()[0]
      .map(function (h) { return String(h).trim().toUpperCase(); });
    for (let c = 0; c < row1.length; c++) {
      if (row1[c].indexOf(TALLY_MARKER) === 0) {
        sheet.deleteColumns(header.length + 1 + c, lastCol - header.length - c);
        break;
      }
    }
  }
  ensureColumns_(sheet, start + width - 1);

  sheet.getRange(1, start, values.length, width).setValues(values);

  // Presentation: title bold, header row bold + centered, subject rows
  // left-aligned + vertically centered — same language as formatTab_.
  sheet.getRange(1, start).setFontWeight('bold');
  const blkHdr = sheet.getRange(2, start, 1, width);
  blkHdr.setFontWeight('bold');
  blkHdr.setHorizontalAlignment(HEADER_HALIGN);
  blkHdr.setVerticalAlignment('middle');
  const blkBody = sheet.getRange(3, start, values.length - 2, width);
  blkBody.setHorizontalAlignment(CONTENT_HALIGN);
  blkBody.setVerticalAlignment('middle');

  for (let r = 0; r < paints.length; r++) {
    if (paints[r].bg) {
      sheet.getRange(r + 3, start, 1, width).setBackground(paints[r].bg);
    }
  }

  return names.length + ' subject row(s)' +
         (capped ? (fullCount ? ', ' + fullCount + ' FULL (limit ' +
                    SUBJECT_LIMIT + ')' : ' — none full yet') : '');
}

/** The sheet's last row that has content in the DATA columns (1..width)
 *  — the tally block's rows must not count, or new submissions would
 *  land below the block (v11). */
function lastDataRow_(sheet, width) {
  const lastRow = sheet.getLastRow();
  if (lastRow <= 0) return 0;
  const rows = sheet.getRange(1, 1, lastRow, Math.max(1, width)).getValues();
  for (let r = rows.length - 1; r >= 0; r--) {
    for (let c = 0; c < rows[r].length; c++) {
      if (String(rows[r][c]).trim() !== '') return r + 1;
    }
  }
  return 0;
}

/** Make sure the sheet's grid is wide enough for column `needed`
 *  (1-based), growing it at the right edge if not. */
function ensureColumns_(sheet, needed) {
  const have = sheet.getMaxColumns();
  if (have < needed) sheet.insertColumnsAfter(have, needed - have);
}

/** Same student already in the sheet with today's DATE? Matches on
 *  STUDENT ID, or — for rows without an ID — on NAME + SECTION. */
function isDuplicate_(sheet, idx, studentId, name, section, stamp) {
  const lastRow = sheet.getLastRow();
  if (lastRow <= 1) return false;

  const today = String(stamp).slice(0, 10);
  if (!/^\d{4}-\d{2}-\d{2}$/.test(today)) return false; // unknown format: allow

  const maxScan = Math.min(lastRow - 1, 500); // recent rows are enough
  const width = Math.max(1, sheet.getLastColumn());
  const rows = sheet.getRange(lastRow - maxScan + 1, 1, maxScan, width).getValues();

  const idKey = 'ID|' + studentId;
  const nsKey = 'NS|' + (name + '|' + section).toUpperCase();
  const idCol = idx['STUDENT ID'];

  for (let i = rows.length - 1; i >= 0; i--) {
    const r = rows[i];
    if (dayString_(r[idx['DATE']]) !== today) continue;
    if (idCol !== undefined &&
        ('ID|' + String(r[idCol] || '').trim()).toUpperCase() === idKey) {
      return true;
    }
    const rowNs = 'NS|' + (String(r[idx['NAME']] || '') + '|' +
                           String(r[idx['SECTION']] || '')).toUpperCase();
    if (rowNs === nsKey) return true;
  }
  return false;
}

/** DATE cells can hold a string ("2026-11-06 21:00:00") or a real Date
 *  object (Sheets converts automatically); normalize both to yyyy-mm-dd. */
function dayString_(v) {
  if (v instanceof Date) {
    return Utilities.formatDate(v, Session.getScriptTimeZone(), 'yyyy-MM-dd');
  }
  const s = String(v == null ? '' : v).trim();
  return /^\d{4}-\d{2}-\d{2}/.test(s) ? s.slice(0, 10) : '';
}

function sanitizeToken(v) {
  const s = String(v || '').replace(/[^A-Za-z0-9]/g, '');
  return s.length ? s : '';
}

/** Tab-safe section token for the tab name (v12): Sheets forbids
 *  \ / ? * [ ] : in tab names — those characters are dropped, runs of
 *  whitespace collapse to one space, and the result is UPPERCASED so
 *  "a1a" and "A1A" land on the same tab instead of splitting the
 *  section in two. The SECTION cell keeps exactly what staff typed;
 *  this only names the tab. */
function sectionTabPart_(section) {
  return String(section || '')
    .replace(/[\\/?*\[\]:]/g, '')
    .replace(/\s+/g, ' ')
    .trim()
    .toUpperCase()
    .slice(0, 40);
}

/** "BSMaRE-1 | A1A" -> "BSMARE-1": the DEFAULT_SUBJECTS key hidden in a
 *  LEGACY tab name (v13 fallback). Section-only names ("A1A") and
 *  unknown names come back unchanged and simply match nothing. Legacy
 *  per-year tab names ("BSMaRE-1") pass through unchanged, so old tabs
 *  keep healing when they hold no data yet. */
function yearKeyForTab_(tabName) {
  return String(tabName).split('|')[0].trim().toUpperCase();
}

/** The DEFAULT_SUBJECTS lists that apply to one tab (v13). The keys —
 *  "BSMARE-1", "BSMT-2", … — are read from the tab's COURSE + YEAR
 *  LEVEL data columns, because the tab name is the section alone now.
 *  A tab holding rows of several course-years gets the UNION of their
 *  lists, in first-seen order. A tab with no usable data rows falls
 *  back to its NAME (covers the legacy per-year and "Course-Year |
 *  Section" layouts this script still heals). Returns the uppercase
 *  subject names in order, or null when nothing matches. */
function defaultSubjectsForTab_(sheet) {
  const header = readHeader_(sheet);
  const courseAt = header.indexOf('COURSE');
  const yearAt = header.indexOf('YEAR LEVEL');
  const keys = [];
  const dataLast = lastDataRow_(sheet, Math.max(1, header.length));
  if (courseAt !== -1 && yearAt !== -1 && dataLast > 1) {
    const n = dataLast - 1;
    const courses = sheet.getRange(2, courseAt + 1, n, 1).getValues();
    const years = sheet.getRange(2, yearAt + 1, n, 1).getValues();
    for (let r = 0; r < n; r++) {
      const c = sanitizeToken(courses[r][0]);
      const y = sanitizeToken(years[r][0]);
      if (!c || !y) continue;
      const key = (c + '-' + y).toUpperCase();
      if (keys.indexOf(key) === -1) keys.push(key);
    }
  }
  if (!keys.length) {
    const byName = DEFAULT_SUBJECTS[yearKeyForTab_(sheet.getName())];
    return byName ? byName.slice() : null;
  }
  const names = [];
  keys.forEach(function (k) {
    (DEFAULT_SUBJECTS[k] || []).forEach(function (n) {
      if (names.indexOf(n) === -1) names.push(n);
    });
  });
  return names.length ? names : null;
}

/** Trim + hard length cap so no hand-crafted payload can bloat a row. */
function clip_(v, n) {
  return String(v == null ? '' : v).trim().slice(0, n);
}

/** Single exit for doPost replies. Since v7 every reply carries the
 *  script version, so the APP can warn when an older script answered —
 *  that mismatch is what re-creates DATE 2 and drops Student ID. */
function reply(obj) {
  obj.ver = obj.ver || CODE_VERSION;
  return ContentService
    .createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}
