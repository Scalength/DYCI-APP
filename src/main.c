/*
 * main.c — GTK3 desktop front end for the DYCI CME enrollment app (v2).
 *
 * The window is branded "DYCI CME APP" (title bar). Since v2.6 the
 * form itself carries no logo and no big title — the DYCI seal (still
 * embedded in the executable) only appears as the small window /
 * taskbar icon, so every pixel of the window goes to the fields.
 *
 * Flow: student information (Student ID + Status 1 / Status 2, then the
 * rest) -> pick course + year level -> the subject
 * list for that program-year appears with an ENROLLED/NOT ENROLLED combo
 * per subject -> Submit.
 *
 * Subjects are editable per program-year ("Edit subjects…" dialog) and
 * persisted in subjects.ini next to the executable.
 *
 * Threading rule: the network call runs in a GThread worker; results are
 * marshalled back with g_idle_add() because GTK widgets must only be
 * touched from the main loop.
 */
#define _POSIX_C_SOURCE 200809L /* readlink, localtime_r under -std=c11 */
#include <gtk/gtk.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "applog.h"
#include "config.h"
#include "enrollment.h"
#include "logo_dyci.h"   /* embedded DYCI seal (generated PNG bytes) */
#include "subjects.h"
#include "sheets_gateway.h"

typedef struct {
    GtkEntry       *last_name;
    GtkEntry       *first_name;
    GtkEntry       *middle_initial;
    GtkEntry       *student_id;
    GtkComboBoxText *status1;      /* OLD / NEW — dropdown under Student ID */
    GtkComboBoxText *status2;      /* REGULAR / IRREGULAR */
    GtkComboBoxText *gender;
    GtkComboBoxText *course;
    GtkComboBoxText *year;
    GtkEntry       *section;
    GtkEntry       *contact;
    GtkEntry       *committee;
    GtkComboBoxText *highlight;    /* NONE / YELLOW / GREEN — row fill flag */

    GtkLabel       *subjects_title;
    GtkBox         *subjects_box;   /* one row per subject, rebuilt live */

    GtkButton      *submit_btn;
    GtkButton      *clear_btn;
    GtkLabel       *status_label;
    GtkSpinner     *spinner;
    GtkWindow      *window;

    SubjectCatalog  catalog;
    char            ini_path[512];
} AppWidgets;

typedef struct {
    AppWidgets *w;
    Enrollment  enrollment;
} SubmitJob;

typedef struct {
    AppWidgets *w;
    long        status;
    char        body[GATEWAY_RESPONSE_MAX];
} SubmitResult;

/* Resolved once at startup: endpoint.txt next to the exe wins over the
 * value compiled into config.h. Read by the worker thread but never
 * written again after startup, so no locking is needed. */
static char g_endpoint[1024];
static int  g_endpoint_from_file = 0;

/* forward declarations (used before their definitions) */
static gboolean submit_finished(SubmitResult *res);
static void on_filter_changed(GtkComboBox *box, gpointer user_data);
static void on_remove_clicked(GtkButton *btn, gpointer user_data);

/* ---------------- small helpers ---------------- */

static void exe_dir_path(char *buf, size_t cap)
{
#ifdef _WIN32
    if (GetModuleFileNameA(NULL, buf, (DWORD)cap) == 0) {
        snprintf(buf, cap, ".");
        return;
    }
#else
    ssize_t n = readlink("/proc/self/exe", buf, cap - 1);
    if (n <= 0) {
        snprintf(buf, cap, ".");
        return;
    }
    buf[n] = '\0';
#endif
    char *slash = strrchr(buf, '/');
#ifdef _WIN32
    {
        char *bslash = strrchr(buf, '\\');
        if (bslash && bslash > slash)
            slash = bslash;
    }
#endif
    if (slash)
        slash[1] = '\0';
    else
        snprintf(buf, cap, ".");
}

/*
 * Picks the endpoint for this machine: the first https:// (or http://)
 * line of endpoint.txt next to the executable wins over the value in
 * config.h, so the URL can be changed on a staff PC without rebuilding.
 * Lines starting with '#' are comments. Returns 1 when the file won.
 */
static int load_endpoint(const char *exe_dir, char *out, size_t cap)
{
    snprintf(out, cap, "%s", SHEETS_ENDPOINT);

    char path[600];
    if (snprintf(path, sizeof path, "%sendpoint.txt",
                 exe_dir) >= (int)sizeof path)
        return 0;

    FILE *fp = fopen(path, "r");
    if (!fp)
        return 0;

    char line[1024];
    int used_file = 0;
    while (fgets(line, sizeof line, fp)) {
        char *s = line;
        while (*s == ' ' || *s == '\t')
            s++;
        if (*s == '\0' || *s == '#' || *s == '\n' || *s == '\r')
            continue; /* blank line or comment */
        size_t n = strlen(s);
        while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' ||
                         s[n - 1] == ' ' || s[n - 1] == '\t'))
            s[--n] = '\0';
        if (strncmp(s, "https://", 8) == 0 ||
            strncmp(s, "http://", 7) == 0) {
            snprintf(out, cap, "%s", s);
            used_file = 1;
        }
        break; /* only the first content line counts */
    }
    fclose(fp);
    return used_file;
}

/*
 * Production safety net: when a submission fails (Wi-Fi down, server
 * error), the exact JSON payload is appended to failed-submissions.jsonl
 * next to the executable, so no data is ever lost. One JSON object per
 * line: {"http":<status>,"json":{...the payload...}}
 */
static void save_outbox(const Enrollment *e, long http_status)
{
    char dir[512], path[600];
    exe_dir_path(dir, sizeof dir);
    if (snprintf(path, sizeof path, "%sfailed-submissions.jsonl",
                 dir) >= (int)sizeof path)
        return;

    char *json = enrollment_to_json(e);
    if (!json) {
        applog("ERROR: outbox: could not serialize the failed enrollment");
        return;
    }

    FILE *fp = fopen(path, "a");
    if (fp) {
        fprintf(fp, "{\"http\":%ld,\"json\":%s}\n", http_status, json);
        fclose(fp);
        applog("saved failed submission to failed-submissions.jsonl (HTTP %ld)",
               http_status);
    } else {
        applog("ERROR: cannot write failed-submissions.jsonl (HTTP %ld)",
               http_status);
    }
    free(json);
}

static void show_error_dialog(GtkWindow *parent, const char *message)
{
    GtkWidget *dlg = gtk_message_dialog_new(parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", message);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

static void set_busy(AppWidgets *w, gboolean busy)
{
    gtk_widget_set_sensitive(GTK_WIDGET(w->submit_btn), !busy);
    gtk_widget_set_sensitive(GTK_WIDGET(w->clear_btn), !busy);
    gtk_widget_set_visible(GTK_WIDGET(w->spinner), busy);
    if (busy)
        gtk_spinner_start(w->spinner);
    else
        gtk_spinner_stop(w->spinner);
}

static int current_program(AppWidgets *w)
{
    int a = gtk_combo_box_get_active(GTK_COMBO_BOX(w->course));
    return a < 0 ? 0 : a;
}

static int current_year_idx(AppWidgets *w) /* 0 = Year 1 */
{
    int a = gtk_combo_box_get_active(GTK_COMBO_BOX(w->year));
    return a < 0 ? 0 : a;
}

static int save_catalog(AppWidgets *w)
{
    char err[ENROLLMENT_ERR_MAX];
    if (subjects_save(&w->catalog, w->ini_path, err, sizeof err) != 0) {
        show_error_dialog(w->window, err);
        return -1;
    }
    return 0;
}

static void destroy_widget_cb(GtkWidget *widget, gpointer data);

static void clear_container(GtkWidget *container)
{
    gtk_container_foreach(GTK_CONTAINER(container), destroy_widget_cb, NULL);
}

static void destroy_widget_cb(GtkWidget *widget, gpointer data)
{
    (void)data;
    gtk_widget_destroy(widget);
}

/* ---------------- dynamic subject rows ---------------- */
static GtkWidget *subject_row_new(const char *name)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget *lbl = gtk_label_new(name);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_widget_set_hexpand(lbl, TRUE);
    gtk_box_pack_start(GTK_BOX(row), lbl, TRUE, TRUE, 0);

    GtkWidget *combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), NULL, STATUS_ENROLLED);
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), NULL, STATUS_NOT_ENROLLED);
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0); /* default: ENROLLED */
    gtk_box_pack_start(GTK_BOX(row), combo, FALSE, FALSE, 0);

    g_object_set_data(G_OBJECT(row), "subject-label", lbl);
    g_object_set_data(G_OBJECT(row), "subject-combo", combo);
    return row;
}

static void rebuild_subject_rows(AppWidgets *w)
{
    int p = current_program(w);
    int y = current_year_idx(w);
    SubjectList *list = subjects_list(&w->catalog, p, y + 1);

    char title[128];
    snprintf(title, sizeof title, "Subjects — %s Year %d  (%d subjects)",
             PROGRAM_NAMES[p], y + 1, list->count);
    gtk_label_set_text(w->subjects_title, title);

    clear_container(GTK_WIDGET(w->subjects_box));
    for (int i = 0; i < list->count; i++)
        gtk_box_pack_start(w->subjects_box,
                           subject_row_new(list->items[i]), FALSE, FALSE, 2);

    gtk_widget_show_all(GTK_WIDGET(w->subjects_box));
}

static void collect_subjects(AppWidgets *w, Enrollment *e)
{
    e->subject_count = 0;

    GList *rows = gtk_container_get_children(GTK_CONTAINER(w->subjects_box));
    for (GList *r = rows; r && e->subject_count < MAX_SUBJECTS; r = r->next) {
        GtkWidget *row = GTK_WIDGET(r->data);
        GtkLabel *lbl = GTK_LABEL(g_object_get_data(G_OBJECT(row),
                                                    "subject-label"));
        GtkComboBox *combo = GTK_COMBO_BOX(g_object_get_data(G_OBJECT(row),
                                                             "subject-combo"));
        if (!lbl || !combo)
            continue;

        SubjectChoice *sc = &e->subjects[e->subject_count++];
        snprintf(sc->name, sizeof sc->name, "%s", gtk_label_get_text(lbl));
        sc->enrolled = (gtk_combo_box_get_active(combo) == 0);
    }
    g_list_free(rows);
}

static void mark_all_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    AppWidgets *w = user_data;
    int active = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "mark"));

    GList *rows = gtk_container_get_children(GTK_CONTAINER(w->subjects_box));
    for (GList *r = rows; r; r = r->next) {
        GtkComboBox *combo = GTK_COMBO_BOX(g_object_get_data(
            G_OBJECT(GTK_WIDGET(r->data)), "subject-combo"));
        if (combo)
            gtk_combo_box_set_active(combo, active);
    }
    g_list_free(rows);
}

/* Course or Year level dropdown changed: show that program-year's subjects. */
static void on_filter_changed(GtkComboBox *box, gpointer user_data)
{
    (void)box;
    AppWidgets *w = user_data;
    rebuild_subject_rows(w);
}

/* ---------------- "Edit subjects" dialog ---------------- */

typedef struct {
    AppWidgets *w;
    GtkBox     *list_box;
    GtkEntry   *add_entry;
    GtkLabel   *msg;
} EditCtx;

static void edit_refresh(EditCtx *ctx)
{
    SubjectList *list = subjects_list(&ctx->w->catalog,
                                      current_program(ctx->w),
                                      current_year_idx(ctx->w) + 1);

    clear_container(GTK_WIDGET(ctx->list_box));
    for (int i = 0; i < list->count; i++) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

        GtkWidget *lbl = gtk_label_new(list->items[i]);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
        gtk_widget_set_hexpand(lbl, TRUE);
        gtk_box_pack_start(GTK_BOX(row), lbl, TRUE, TRUE, 0);

        GtkWidget *rm = gtk_button_new_with_label("Remove");
        g_object_set_data(G_OBJECT(rm), "edit-ctx", ctx);
        gtk_box_pack_start(GTK_BOX(row), rm, FALSE, FALSE, 0);
        g_signal_connect(rm, "clicked",
                         G_CALLBACK(on_remove_clicked), GINT_TO_POINTER(i));

        gtk_box_pack_start(ctx->list_box, row, FALSE, FALSE, 2);
    }
    gtk_widget_show_all(GTK_WIDGET(ctx->list_box));
}

static void on_remove_clicked(GtkButton *btn, gpointer user_data)
{
    int index = GPOINTER_TO_INT(user_data);
    EditCtx *ctx = g_object_get_data(G_OBJECT(btn), "edit-ctx");
    if (!ctx)
        return;

    SubjectList *list = subjects_list(&ctx->w->catalog,
                                      current_program(ctx->w),
                                      current_year_idx(ctx->w) + 1);
    if (subjects_remove(list, index) == 0) {
        if (save_catalog(ctx->w) == 0) {
            edit_refresh(ctx);
            rebuild_subject_rows(ctx->w);
        }
    }
}

static void on_add_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    EditCtx *ctx = user_data;
    SubjectList *list = subjects_list(&ctx->w->catalog,
                                      current_program(ctx->w),
                                      current_year_idx(ctx->w) + 1);

    int rc = subjects_add(list, gtk_entry_get_text(ctx->add_entry));
    if (rc == 0) {
        if (save_catalog(ctx->w) == 0) {
            edit_refresh(ctx);
            rebuild_subject_rows(ctx->w);
            gtk_entry_set_text(ctx->add_entry, "");
            gtk_widget_grab_focus(GTK_WIDGET(ctx->add_entry));
        }
        gtk_label_set_text(ctx->msg, "");
    } else if (rc == 1) {
        gtk_label_set_text(ctx->msg, "That subject is already in the list.");
    } else {
        gtk_label_set_text(ctx->msg,
                           "Enter a name, or the list is full (max 48).");
    }
}

static void on_edit_subjects(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    AppWidgets *w = user_data;

    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        "Edit subjects", w->window,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close", GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 400, 440);

    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(area), 12);
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_pack_start(GTK_BOX(area), vbox, TRUE, TRUE, 0);

    GtkWidget *hint = gtk_label_new(NULL);
    char buf[160];
    snprintf(buf, sizeof buf,
             "<b>%s Year %d</b> — subjects shown on the form for this "
             "course and year.", PROGRAM_NAMES[current_program(w)],
             current_year_idx(w) + 1);
    gtk_label_set_markup(GTK_LABEL(hint), buf);
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), hint, FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 260);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    GtkWidget *list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(scroll), list_box);

    GtkWidget *add_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *add_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(add_entry), "New subject, e.g. NAV 3");
    gtk_entry_set_max_length(GTK_ENTRY(add_entry), SUBJECT_NAME_MAX - 1);
    gtk_widget_set_hexpand(add_entry, TRUE);
    gtk_box_pack_start(GTK_BOX(add_row), add_entry, TRUE, TRUE, 0);
    GtkWidget *add_btn = gtk_button_new_with_label("Add subject");
    gtk_box_pack_start(GTK_BOX(add_row), add_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), add_row, FALSE, FALSE, 0);

    GtkWidget *msg = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(msg), 0.0);
    gtk_box_pack_start(GTK_BOX(vbox), msg, FALSE, FALSE, 0);

    EditCtx ctx = { w, GTK_BOX(list_box), GTK_ENTRY(add_entry), GTK_LABEL(msg) };
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_add_clicked), &ctx);
    g_signal_connect(add_entry, "activate", G_CALLBACK(on_add_clicked), &ctx);

    edit_refresh(&ctx);
    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

/* ---------------- submit pipeline ---------------- */

/* Human-readable hint for the most common transport failures. */
static const char *curl_hint(long code)
{
    switch (code < 0 ? -code : code) {
    case 6:
        return "Could not resolve the server name "
               "- check your internet connection.";
    case 7:
        return "Could not connect to Google "
               "- check your internet or firewall.";
    case 28:
        return "Connection timed out "
               "- the network may be slow or blocking the request.";
    case 60:
    case 77:
        return "The secure connection could not be verified "
               "- the file ca-bundle.crt must sit next to the app "
               "(the portable package includes it; do not delete it).";
    default:
        return "Check your internet connection and the URL in endpoint.txt / "
               "src/config.h.";
    }
}

static void *submit_worker(void *data)
{
    SubmitJob *job = data;

    char name[256];
    enrollment_format_name(&job->enrollment, name, sizeof name);
    applog("submitting \"%s\" (%s year %s, %d subjects)\xE2\x80\xA6",
           name, job->enrollment.course, job->enrollment.year_level,
           job->enrollment.subject_count);

    SubmitResult *res = g_malloc0(sizeof *res);
    res->w = job->w;
    res->status = sheets_gateway_submit(&job->enrollment, g_endpoint,
                                        res->body, sizeof res->body);
    if (res->status != 200)
        save_outbox(&job->enrollment, res->status);
    g_free(job);

    /* GTK is not thread-safe: never touch widgets here. */
    g_idle_add((GSourceFunc)submit_finished, res);
    return NULL;
}

/* The deployed script's self-reported version: every Code.gs reply
 * (v7+) carries "ver":"vN". Returns 1 and fills `ver` when found;
 * returns 0 when the reply has no version at all — that means a pre-v7
 * script answered, i.e. the endpoint is running old code. An old script
 * is exactly what ignores Student ID, re-creates DATE 2 and writes
 * subjects without labels, so the UI warns loudly about it. */
static int script_version_of(const char *body, char *ver, size_t cap)
{
    const char *tag = strstr(body, "\"ver\":\"");
    if (!tag)
        return 0;
    tag += 7; /* skip "ver":" */
    size_t i = 0;
    while (tag[i] != '\0' && tag[i] != '"' && i + 1 < cap) {
        ver[i] = tag[i];
        i++;
    }
    ver[i] = '\0';
    return 1;
}

/* The server's human-readable reason from a {"status":"error",
 * "message":"..."} reply (Code.gs v10 sends one when a subject hit
 * its 40-enrollment hard limit). Same no-escape assumption as above:
 * the server never puts quotes or backslashes in its messages.
 * Returns 1 and fills `msg` when a message is present. */
static int server_message_of(const char *body, char *msg, size_t cap)
{
    const char *tag = strstr(body, "\"message\":\"");
    if (!tag)
        return 0;
    tag += 11; /* skip "message":" */
    size_t i = 0;
    while (tag[i] != '\0' && tag[i] != '"' && i + 1 < cap) {
        msg[i] = tag[i];
        i++;
    }
    msg[i] = '\0';
    return i > 0;
}

static gboolean submit_finished(SubmitResult *res)
{
    AppWidgets *w = res->w;
    char msg[768];

    set_busy(w, FALSE);

    applog("submit result: HTTP %ld", res->status);

    char script_ver[16];
    const int have_ver = (res->status == 200)
        ? script_version_of(res->body, script_ver, sizeof script_ver) : 0;
    const int script_ok = have_ver &&
                          strcmp(script_ver, SHEETS_SCRIPT_VER) == 0;
    if (res->status == 200)
        applog("script that answered: %s", have_ver ? script_ver
                                                    : "unknown (old Code.gs)");

    if (res->status == 200 &&
        strstr(res->body, "\"duplicate\":true") != NULL) {
        if (script_ok) {
            gtk_label_set_markup(w->status_label,
                "<span foreground=\"#b26a00\" font_weight=\"bold\">"
                "Already submitted today for this student \xE2\x80\x94 no second "
                "row was added.</span>");
        } else {
            /* An old script answered: it neither repairs the tab nor
             * reports a version. Point at the fix right away. */
            snprintf(msg, sizeof msg,
                "<span foreground=\"#b26a00\" font_weight=\"bold\">"
                "Already submitted today for this student \xE2\x80\x94 no second "
                "row was added.</span> "
                "<span foreground=\"#c62828\" font_weight=\"bold\">"
                "BUT an OLD spreadsheet script answered (%s). It never "
                "repairs the tab \xE2\x80\x94 update Code.gs and redeploy: "
                "Deploy \xE2\x86\x92 Manage deployments \xE2\x86\x92 "
                "\xE2\x9C\x8F \xE2\x86\x92 Version: New version.</span>",
                have_ver ? script_ver : "no version reported");
            gtk_label_set_markup(w->status_label, msg);
        }
        applog("duplicate submission blocked by the server");
    } else if (res->status == 200 &&
               strstr(res->body, "\"status\":\"error\"") != NULL) {
        /* Code.gs answers HTTP 200 even for rejections (v10: a full
         * subject, bad token, missing fields). Show the server's own
         * reason instead of pretending the submit worked. The form is
         * NOT cleared, so the pick can be corrected and resent. */
        char reason[384] = "";
        const int have_reason =
            server_message_of(res->body, reason, sizeof reason);
        snprintf(msg, sizeof msg,
            "<span foreground=\"#c62828\" font_weight=\"bold\">"
            "Not saved \xE2\x80\x94 the spreadsheet rejected this "
            "submission.%s%s%s</span>",
            have_reason ? " Reason: " : "",
            have_reason ? reason : "",
            have_reason ? "" : " (no reason reported)");
        gtk_label_set_markup(w->status_label, msg);
        applog("server rejected the submission: %s",
               have_reason ? reason : "no reason reported");
    } else if (res->status == 200) {
        if (script_ok) {
            gtk_label_set_markup(w->status_label,
                "<span foreground=\"#1a7f37\" font_weight=\"bold\">"
                "\xE2\x9C\x93 Submitted! Use \"Clear form\" for the next student."
                "</span>");
        } else if (have_ver) {
            snprintf(msg, sizeof msg,
                "<span foreground=\"#1a7f37\" font_weight=\"bold\">"
                "\xE2\x9C\x93 Saved.</span> "
                "<span foreground=\"#b26a00\" font_weight=\"bold\">"
                "BUT the spreadsheet script is %s while this app expects "
                "%s \xE2\x80\x94 update Code.gs and redeploy: Deploy "
                "\xE2\x86\x92 Manage deployments \xE2\x86\x92 \xE2\x9C\x8F "
                "\xE2\x86\x92 Version: New version \xE2\x86\x92 Deploy.</span>",
                script_ver, SHEETS_SCRIPT_VER);
            gtk_label_set_markup(w->status_label, msg);
        } else {
            snprintf(msg, sizeof msg,
                "<span foreground=\"#1a7f37\" font_weight=\"bold\">"
                "\xE2\x9C\x93 Saved.</span> "
                "<span foreground=\"#c62828\" font_weight=\"bold\">"
                "BUT an OLD spreadsheet script answered \xE2\x80\x94 that is "
                "what ignores Student ID and re-creates DATE 2 with "
                "unlabeled subjects. Fix: Extensions \xE2\x86\x92 Apps "
                "Script \xE2\x86\x92 replace ALL code with the new Code.gs, "
                "then Deploy \xE2\x86\x92 Manage deployments \xE2\x86\x92 "
                "\xE2\x9C\x8F \xE2\x86\x92 Version: New version \xE2\x86\x92 "
                "Deploy.</span>");
            gtk_label_set_markup(w->status_label, msg);
        }
    } else if (res->status < 0) {
        snprintf(msg, sizeof msg,
            "<span foreground=\"#c62828\">Network error (curl code %ld). "
            "%s</span>", res->status, curl_hint(res->status));
        gtk_label_set_markup(w->status_label, msg);
    } else {
        snprintf(msg, sizeof msg,
            "<span foreground=\"#c62828\">Server replied HTTP %ld. "
            "Check that the Apps Script deployment is live and "
            "&#8220;Who has access&#8221; is set to &#8220;Anyone&#8221;.</span>",
            res->status);
        gtk_label_set_markup(w->status_label, msg);
    }

    g_free(res);
    return G_SOURCE_REMOVE;
}

static void on_submit_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    AppWidgets *w = user_data;

    Enrollment e = {0};

    snprintf(e.last_name,  sizeof e.last_name,  "%s", gtk_entry_get_text(w->last_name));
    snprintf(e.first_name, sizeof e.first_name, "%s", gtk_entry_get_text(w->first_name));
    snprintf(e.middle_initial, sizeof e.middle_initial, "%s",
             gtk_entry_get_text(w->middle_initial));
    snprintf(e.student_id, sizeof e.student_id, "%s", gtk_entry_get_text(w->student_id));

    gchar *s1 = gtk_combo_box_text_get_active_text(w->status1);
    snprintf(e.status1, sizeof e.status1, "%s", s1 ? s1 : "");
    g_free(s1);
    gchar *s2 = gtk_combo_box_text_get_active_text(w->status2);
    snprintf(e.status2, sizeof e.status2, "%s", s2 ? s2 : "");
    g_free(s2);

    gchar *g = gtk_combo_box_text_get_active_text(w->gender);
    snprintf(e.gender,  sizeof e.gender,  "%s", g ? g : "");
    g_free(g);

    snprintf(e.course, sizeof e.course, "%s", PROGRAM_NAMES[current_program(w)]);
    snprintf(e.year_level, sizeof e.year_level, "%d", current_year_idx(w) + 1);

    snprintf(e.section,   sizeof e.section,   "%s", gtk_entry_get_text(w->section));
    snprintf(e.contact_number, sizeof e.contact_number, "%s",
             gtk_entry_get_text(w->contact));
    snprintf(e.committee, sizeof e.committee, "%s", gtk_entry_get_text(w->committee));

    gchar *hl = gtk_combo_box_text_get_active_text(w->highlight);
    snprintf(e.highlight, sizeof e.highlight, "%s", hl ? hl : HIGHLIGHT_NONE);
    g_free(hl);

    time_t now = time(NULL);
    struct tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif
    strftime(e.submitted_at, sizeof e.submitted_at, "%Y-%m-%d %H:%M:%S", &tm_now);

    collect_subjects(w, &e);

    char err[ENROLLMENT_ERR_MAX];
    if (enrollment_validate(&e, err, sizeof err) != 0) {
        applog("validation failed: %s", err);
        show_error_dialog(w->window, err);
        gtk_label_set_markup(w->status_label,
            "<span foreground=\"#c62828\">The enrollment was not sent.</span>");
        return;
    }

    if (g_endpoint[0] == '\0' ||
        strstr(g_endpoint, "PASTE_YOUR_APPS_SCRIPT_EXEC_URL_HERE") != NULL) {
        show_error_dialog(w->window,
            "Endpoint not configured yet.\n\n"
            "Put your Apps Script /exec URL into endpoint.txt\n"
            "(a text file next to the app, one line) or into\n"
            "src/config.h, then restart the app. See README.md.");
        return;
    }

    SubmitJob *job = g_new(SubmitJob, 1);
    job->w = w;
    job->enrollment = e;

    set_busy(w, TRUE);
    gtk_label_set_text(w->status_label, "Submitting\xE2\x80\xA6");
    g_thread_new("submit", submit_worker, job);
}

static void on_clear_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    AppWidgets *w = user_data;

    gtk_entry_set_text(w->last_name, "");
    gtk_entry_set_text(w->first_name, "");
    gtk_entry_set_text(w->middle_initial, "");
    gtk_entry_set_text(w->student_id, "");
    gtk_combo_box_set_active(GTK_COMBO_BOX(w->status1), -1); /* no pick yet */
    gtk_combo_box_set_active(GTK_COMBO_BOX(w->status2), -1);
    gtk_combo_box_set_active(GTK_COMBO_BOX(w->gender), 0);
    gtk_combo_box_set_active(GTK_COMBO_BOX(w->course), 0);
    gtk_combo_box_set_active(GTK_COMBO_BOX(w->year), 0);
    gtk_entry_set_text(w->section, "");
    gtk_entry_set_text(w->contact, "");
    gtk_entry_set_text(w->committee, "");
    gtk_combo_box_set_active(GTK_COMBO_BOX(w->highlight), 0); /* NONE */

    rebuild_subject_rows(w);
    gtk_label_set_text(w->status_label, "");
    gtk_widget_grab_focus(GTK_WIDGET(w->last_name));
}

/* ---------------- window ---------------- */

/* Decodes the PNG bytes embedded from logo_dyci.h (the DYCI seal, with
 * its transparent background preserved) into a pixbuf. Returns a
 * caller-owned reference, or NULL — the form then just renders without
 * the logo instead of failing. */
static GdkPixbuf *load_logo_pixbuf(void)
{
    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
    GError *err = NULL;
    GdkPixbuf *pix = NULL;

    if (gdk_pixbuf_loader_write(loader, LOGO_DYCI_PNG, LOGO_DYCI_PNG_LEN,
                                &err) &&
        gdk_pixbuf_loader_close(loader, &err)) {
        pix = gdk_pixbuf_loader_get_pixbuf(loader);
        if (pix)
            g_object_ref(pix); /* detach from the loader */
    }
    if (!pix)
        applog("ERROR: could not decode the embedded DYCI seal: %s",
               err && err->message ? err->message : "unknown error");
    if (err)
        g_error_free(err);
    g_object_unref(loader);
    return pix;
}

static GtkWidget *info_row(GtkWidget *grid, int row, const char *label,
                           GtkWidget *field)
{
    GtkWidget *lbl = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(lbl), 1.0);
    gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
    gtk_widget_set_hexpand(field, TRUE);
    gtk_grid_attach(GTK_GRID(grid), field, 1, row, 1, 1);
    return field;
}

static void activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;
    AppWidgets *w = g_new0(AppWidgets, 1);

    char exe_dir[512];
    exe_dir_path(exe_dir, sizeof exe_dir);

    applog_open(exe_dir);
    g_endpoint_from_file = load_endpoint(exe_dir, g_endpoint, sizeof g_endpoint);
    applog("DYCI CME APP v%s \xE2\x80\x94 endpoint: %s", APP_VERSION,
           g_endpoint_from_file ? "endpoint.txt" : "built-in (config.h)");

    snprintf(w->ini_path, sizeof w->ini_path, "%s%s", exe_dir,
             SUBJECTS_INI_NAME);

    char err[ENROLLMENT_ERR_MAX];
    if (subjects_load(&w->catalog, w->ini_path, err, sizeof err) != 0) {
        applog("subjects.ini problem: %s", err);
        show_error_dialog(NULL, err); /* window not built yet; still fine */
    }

    GtkWidget *win = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(win), "DYCI CME APP");
    gtk_window_set_default_size(GTK_WINDOW(win), 560, 760);
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);
    w->window = GTK_WINDOW(win);

    /* Clean white backdrop instead of the default theme grey. */
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "window { background: #ffffff; }", -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(win), scrolled);

    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(page), 12);
    gtk_container_add(GTK_CONTAINER(scrolled), page);

    /* --- No big logo / title inside the form (v2.6): the embedded
     * DYCI seal only survives as the small title-bar + taskbar icon. */
    GdkPixbuf *logo = load_logo_pixbuf();
    if (logo) {
        gtk_window_set_default_icon(logo); /* title bar + taskbar */
        g_object_unref(logo); /* the icon keeps its own reference */
    }

    /* --- Student information --- */
    GtkWidget *frame1 = gtk_frame_new("Student information");
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    gtk_container_add(GTK_CONTAINER(frame1), grid);
    gtk_box_pack_start(GTK_BOX(page), frame1, FALSE, FALSE, 0);

    w->last_name = GTK_ENTRY(info_row(grid, 0, "Last name:", gtk_entry_new()));
    gtk_entry_set_placeholder_text(w->last_name, "e.g. Dazal");
    w->first_name = GTK_ENTRY(info_row(grid, 1, "First name:", gtk_entry_new()));
    gtk_entry_set_placeholder_text(w->first_name, "e.g. Sean Kenneth");
    w->middle_initial = GTK_ENTRY(info_row(grid, 2, "Middle initial:", gtk_entry_new()));
    gtk_entry_set_placeholder_text(w->middle_initial, "e.g. J (optional)");
    gtk_entry_set_max_length(w->middle_initial, 3);

    w->student_id = GTK_ENTRY(info_row(grid, 3, "Student ID:", gtk_entry_new()));
    gtk_entry_set_placeholder_text(w->student_id, "e.g. 2026-0000");
    /* Plain text box: no live formatting, no shape validation. The cap
     * below only stops typing beyond the buffer (see enrollment.h). */
    gtk_entry_set_max_length(w->student_id, ENROLLMENT_FIELD_MAX - 1);

    /* STATUS 1 / STATUS 2 — right below the Student ID box. No default:
     * staff must actively pick both; the validator rejects a submission
     * with either left empty. */
    GtkWidget *status1_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(status1_combo), NULL,
                              STATUS1_OLD);
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(status1_combo), NULL,
                              STATUS1_NEW);
    w->status1 = GTK_COMBO_BOX_TEXT(status1_combo);
    info_row(grid, 4, "Status 1:", status1_combo);

    GtkWidget *status2_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(status2_combo), NULL,
                              STATUS2_REGULAR);
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(status2_combo), NULL,
                              STATUS2_IRREGULAR);
    w->status2 = GTK_COMBO_BOX_TEXT(status2_combo);
    info_row(grid, 5, "Status 2:", status2_combo);

    GtkWidget *gender_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(gender_combo), NULL, GENDER_MALE);
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(gender_combo), NULL, GENDER_FEMALE);
    gtk_combo_box_set_active(GTK_COMBO_BOX(gender_combo), 0);
    w->gender = GTK_COMBO_BOX_TEXT(gender_combo);
    info_row(grid, 6, "Gender:", gender_combo);

    GtkWidget *course_combo = gtk_combo_box_text_new();
    for (int p = 0; p < PROGRAM_COUNT; p++)
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(course_combo), NULL,
                                  PROGRAM_NAMES[p]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(course_combo), 0);
    w->course = GTK_COMBO_BOX_TEXT(course_combo);
    info_row(grid, 7, "Course:", course_combo);

    GtkWidget *year_combo = gtk_combo_box_text_new();
    for (int y = 1; y <= YEAR_COUNT; y++) {
        char ytxt[8];
        snprintf(ytxt, sizeof ytxt, "%d", y);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(year_combo), NULL, ytxt);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(year_combo), 0);
    w->year = GTK_COMBO_BOX_TEXT(year_combo);
    info_row(grid, 8, "Year level:", year_combo);

    w->section = GTK_ENTRY(info_row(grid, 9, "Section:", gtk_entry_new()));
    gtk_entry_set_placeholder_text(w->section, "e.g. A");

    w->contact = GTK_ENTRY(info_row(grid, 10, "Contact number:", gtk_entry_new()));
    gtk_entry_set_placeholder_text(w->contact, "e.g. 0917 123 4567");
    gtk_entry_set_max_length(w->contact, CONTACT_MAX - 1);

    w->committee = GTK_ENTRY(info_row(grid, 11, "Enrollment committee:", gtk_entry_new()));
    gtk_entry_set_placeholder_text(w->committee, "e.g. Committee 1");

    /* Highlight (optional): paints the student's whole row in the
     * sheet — e.g. YELLOW while a record has an error, GREEN once it
     * is resolved. The HIGHLIGHT column in the sheet carries the same
     * dropdown, so staff can re-flag a row later; blank cells never
     * touch the fill, so painting rows by hand stays safe. */
    GtkWidget *highlight_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(highlight_combo), NULL,
                              HIGHLIGHT_NONE);
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(highlight_combo), NULL,
                              HIGHLIGHT_YELLOW);
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(highlight_combo), NULL,
                              HIGHLIGHT_GREEN);
    gtk_combo_box_set_active(GTK_COMBO_BOX(highlight_combo), 0);
    w->highlight = GTK_COMBO_BOX_TEXT(highlight_combo);
    info_row(grid, 12, "Highlight:", highlight_combo);

    g_signal_connect(course_combo, "changed",
                     G_CALLBACK(on_filter_changed), w);
    g_signal_connect(year_combo, "changed",
                     G_CALLBACK(on_filter_changed), w);

    /* --- Subjects (dynamic) --- */
    GtkWidget *frame2 = gtk_frame_new("Subjects");
    GtkWidget *sub_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(sub_vbox), 10);
    gtk_container_add(GTK_CONTAINER(frame2), sub_vbox);
    gtk_box_pack_start(GTK_BOX(page), frame2, FALSE, FALSE, 0);

    w->subjects_title = GTK_LABEL(gtk_label_new("Subjects"));
    gtk_label_set_xalign(w->subjects_title, 0.0);
    gtk_box_pack_start(GTK_BOX(sub_vbox), GTK_WIDGET(w->subjects_title),
                       FALSE, FALSE, 0);

    w->subjects_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    gtk_box_pack_start(GTK_BOX(sub_vbox), GTK_WIDGET(w->subjects_box),
                       FALSE, FALSE, 0);

    GtkWidget *btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *all_enrolled = gtk_button_new_with_label("Mark all ENROLLED");
    g_object_set_data(G_OBJECT(all_enrolled), "mark", GINT_TO_POINTER(0));
    g_signal_connect(all_enrolled, "clicked", G_CALLBACK(mark_all_clicked), w);
    gtk_box_pack_start(GTK_BOX(btn_row), all_enrolled, FALSE, FALSE, 0);

    GtkWidget *all_not = gtk_button_new_with_label("Mark all NOT ENROLLED");
    g_object_set_data(G_OBJECT(all_not), "mark", GINT_TO_POINTER(1));
    g_signal_connect(all_not, "clicked", G_CALLBACK(mark_all_clicked), w);
    gtk_box_pack_start(GTK_BOX(btn_row), all_not, FALSE, FALSE, 0);

    GtkWidget *edit_btn = gtk_button_new_with_label("Edit subjects\xE2\x80\xA6");
    g_signal_connect(edit_btn, "clicked", G_CALLBACK(on_edit_subjects), w);
    gtk_box_pack_start(GTK_BOX(btn_row), edit_btn, FALSE, FALSE, 0);

    GtkWidget *btn_row_wrap = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(btn_row_wrap), btn_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sub_vbox), btn_row_wrap, FALSE, FALSE, 0);

    /* --- Submit / status --- */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    w->spinner = GTK_SPINNER(gtk_spinner_new());
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(w->spinner), FALSE, FALSE, 0);
    GtkWidget *status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(status), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(status), TRUE);
    gtk_widget_set_hexpand(status, TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), status, TRUE, TRUE, 0);
    w->status_label = GTK_LABEL(status);
    gtk_box_pack_start(GTK_BOX(page), hbox, FALSE, FALSE, 0);

    GtkWidget *submit_btn = gtk_button_new_with_label("Submit enrollment");
    gtk_widget_set_hexpand(submit_btn, TRUE);
    w->submit_btn = GTK_BUTTON(submit_btn);
    gtk_box_pack_start(GTK_BOX(page), submit_btn, FALSE, FALSE, 0);
    g_signal_connect(submit_btn, "clicked", G_CALLBACK(on_submit_clicked), w);

    GtkWidget *clear_btn = gtk_button_new_with_label("Clear form");
    w->clear_btn = GTK_BUTTON(clear_btn);
    gtk_box_pack_start(GTK_BOX(page), clear_btn, FALSE, FALSE, 0);
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear_clicked), w);

    rebuild_subject_rows(w);

    gtk_widget_show_all(win);
    gtk_widget_set_visible(GTK_WIDGET(w->spinner), FALSE);
}

int main(int argc, char **argv)
{
    sheets_gateway_init();

    GtkApplication *app =
        gtk_application_new("ph.dyci.cme.enrollment", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);
    sheets_gateway_cleanup();
    return status;
}
