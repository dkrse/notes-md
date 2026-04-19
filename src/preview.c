#define _GNU_SOURCE
#include "preview.h"
#include "settings.h"
#include <adwaita.h>
#include <webkit/webkit.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <poppler.h>
#include <cairo.h>
#include <cairo-pdf.h>
#include <glib/gstdio.h>

/* Resolve data/webview dir next to the executable. */
static char *resolve_webview_dir(void) {
    char exe[1024];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) return NULL;
    exe[n] = '\0';
    char *slash = strrchr(exe, '/');
    if (slash) *slash = '\0';

    /* Try sibling layout (build/notes-md + data/webview) first, then local */
    char cand[2048];
    snprintf(cand, sizeof(cand), "%s/../data/webview", exe);
    if (g_file_test(cand, G_FILE_TEST_IS_DIR)) return g_strdup(cand);
    snprintf(cand, sizeof(cand), "%s/data/webview", exe);
    if (g_file_test(cand, G_FILE_TEST_IS_DIR)) return g_strdup(cand);
    return NULL;
}

/* JSON-string-encode buffer text for safe JS embedding. */
static char *json_escape(const char *s) {
    GString *out = g_string_new("\"");
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
            case '"':  g_string_append(out, "\\\""); break;
            case '\\': g_string_append(out, "\\\\"); break;
            case '\n': g_string_append(out, "\\n");  break;
            case '\r': g_string_append(out, "\\r");  break;
            case '\t': g_string_append(out, "\\t");  break;
            case '\b': g_string_append(out, "\\b");  break;
            case '\f': g_string_append(out, "\\f");  break;
            default:
                if (*p < 0x20) g_string_append_printf(out, "\\u%04x", *p);
                else g_string_append_c(out, *p);
        }
    }
    g_string_append_c(out, '"');
    return g_string_free(out, FALSE);
}

static gboolean is_dark_theme_name(const char *t) {
    if (!t) return FALSE;
    return strstr(t, "dark") != NULL || !strcmp(t, "monokai") ||
           !strcmp(t, "nord") || !strcmp(t, "dracula") ||
           !strcmp(t, "tokyo-night") || !strcmp(t, "catppuccin-mocha");
}

static void push_text(NotesWindow *win) {
    if (!win->preview_webview || !win->preview_ready) return;
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(win->buffer, &s, &e);
    char *txt = gtk_text_buffer_get_text(win->buffer, &s, &e, FALSE);
    char *enc = json_escape(txt ? txt : "");
    char *js  = g_strdup_printf("window.nmdRender(%s);", enc);
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(win->preview_webview),
                                        js, -1, NULL, NULL, NULL, NULL, NULL);
    g_free(js); g_free(enc); g_free(txt);
}

static gboolean update_timeout_cb(gpointer user) {
    NotesWindow *win = user;
    win->preview_update_id = 0;
    push_text(win);
    return G_SOURCE_REMOVE;
}

void preview_queue_update(NotesWindow *win) {
    if (!win->preview_webview) return;
    if (win->preview_update_id) g_source_remove(win->preview_update_id);
    win->preview_update_id = g_timeout_add(250, update_timeout_cb, win);
}

void preview_apply_theme(NotesWindow *win) {
    if (!win->preview_webview || !win->preview_ready) return;
    gboolean dark = is_dark_theme_name(win->settings.theme);
    if (!strcmp(win->settings.theme, "system") ||
        !strcmp(win->settings.theme, "light") ||
        !strcmp(win->settings.theme, "dark")) {
        AdwStyleManager *sm = adw_style_manager_get_default();
        dark = adw_style_manager_get_dark(sm);
    }
    char *js = g_strdup_printf("window.nmdSetTheme(%s);", dark ? "true" : "false");
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(win->preview_webview),
                                        js, -1, NULL, NULL, NULL, NULL, NULL);
    g_free(js);
}

void preview_apply_layout(NotesWindow *win) {
    if (!win->preview_webview || !win->preview_ready) return;
    char *js = g_strdup_printf("window.nmdSetLayout(%s);",
                               win->settings.preview_full_width ? "true" : "false");
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(win->preview_webview),
                                        js, -1, NULL, NULL, NULL, NULL, NULL);
    g_free(js);
}

void preview_apply_font_size(NotesWindow *win) {
    if (!win->preview_webview) return;
    double z = (double)win->settings.preview_font_size / 14.0;
    if (z < 0.4) z = 0.4;
    if (z > 4.0) z = 4.0;
    win->preview_zoom = z;
    webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(win->preview_webview), z);
}

/* ── Find in preview ── */

static void on_find_entry_changed(GtkEditable *entry, gpointer user) {
    NotesWindow *win = user;
    const char *text = gtk_editable_get_text(entry);
    WebKitFindController *fc = webkit_web_view_get_find_controller(
        WEBKIT_WEB_VIEW(win->preview_webview));
    if (text[0] == '\0') {
        webkit_find_controller_search_finish(fc);
        gtk_label_set_text(win->preview_find_label, "");
        return;
    }
    webkit_find_controller_search(fc, text,
        WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE | WEBKIT_FIND_OPTIONS_WRAP_AROUND,
        G_MAXUINT);
}

static void on_find_next(GtkButton *b, gpointer user) {
    (void)b;
    NotesWindow *win = user;
    WebKitFindController *fc = webkit_web_view_get_find_controller(
        WEBKIT_WEB_VIEW(win->preview_webview));
    webkit_find_controller_search_next(fc);
}

static void on_find_prev(GtkButton *b, gpointer user) {
    (void)b;
    NotesWindow *win = user;
    WebKitFindController *fc = webkit_web_view_get_find_controller(
        WEBKIT_WEB_VIEW(win->preview_webview));
    webkit_find_controller_search_previous(fc);
}

static void on_find_close(GtkButton *b, gpointer user) {
    (void)b;
    preview_find_hide(user);
}

static gboolean on_find_entry_key(GtkEventControllerKey *c, guint keyval,
                                   guint kc, GdkModifierType mods, gpointer user) {
    (void)c; (void)kc; (void)mods;
    if (keyval == GDK_KEY_Escape) { preview_find_hide(user); return TRUE; }
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        NotesWindow *win = user;
        WebKitFindController *fc = webkit_web_view_get_find_controller(
            WEBKIT_WEB_VIEW(win->preview_webview));
        if (mods & GDK_SHIFT_MASK)
            webkit_find_controller_search_previous(fc);
        else
            webkit_find_controller_search_next(fc);
        return TRUE;
    }
    return FALSE;
}

static void on_find_matches(WebKitFindController *fc, guint count, gpointer user) {
    (void)fc;
    NotesWindow *win = user;
    char buf[64];
    snprintf(buf, sizeof(buf), "%u match%s", count, count == 1 ? "" : "es");
    gtk_label_set_text(win->preview_find_label, buf);
}

static void on_find_failed(WebKitFindController *fc, gpointer user) {
    (void)fc;
    NotesWindow *win = user;
    gtk_label_set_text(win->preview_find_label, "Not found");
}

void preview_find_show(NotesWindow *win) {
    if (!win->preview_find_bar) return;
    gtk_widget_set_visible(win->preview_find_bar, TRUE);
    gtk_editable_select_region(GTK_EDITABLE(win->preview_find_entry), 0, -1);
    gtk_widget_grab_focus(win->preview_find_entry);
    /* Retrigger search if there was text */
    on_find_entry_changed(GTK_EDITABLE(win->preview_find_entry), win);
}

void preview_find_hide(NotesWindow *win) {
    if (!win->preview_find_bar) return;
    WebKitFindController *fc = webkit_web_view_get_find_controller(
        WEBKIT_WEB_VIEW(win->preview_webview));
    webkit_find_controller_search_finish(fc);
    gtk_widget_set_visible(win->preview_find_bar, FALSE);
    gtk_widget_grab_focus(win->preview_webview);
}

/* ── PDF export ── */

static void pdf_add_page_numbers(const char *pdf_path, const char *mode) {
    if (!mode || !strcmp(mode, "none")) return;

    char *uri = g_filename_to_uri(pdf_path, NULL, NULL);
    if (!uri) return;
    PopplerDocument *doc = poppler_document_new_from_file(uri, NULL, NULL);
    g_free(uri);
    if (!doc) return;

    int n_pages = poppler_document_get_n_pages(doc);
    if (n_pages == 0) { g_object_unref(doc); return; }

    char *tmp_path = g_strdup_printf("%s.XXXXXX", pdf_path);
    int tmp_fd = g_mkstemp(tmp_path);
    if (tmp_fd < 0) {
        g_free(tmp_path);
        g_object_unref(doc);
        return;
    }
    close(tmp_fd);  /* cairo_pdf_surface_create re-opens by path */
    g_chmod(tmp_path, 0600);

    PopplerPage *first = poppler_document_get_page(doc, 0);
    double pw, ph;
    poppler_page_get_size(first, &pw, &ph);
    g_object_unref(first);

    cairo_surface_t *surface = cairo_pdf_surface_create(tmp_path, pw, ph);
    cairo_t *cr = cairo_create(surface);

    for (int i = 0; i < n_pages; i++) {
        PopplerPage *page = poppler_document_get_page(doc, i);
        double w, h;
        poppler_page_get_size(page, &w, &h);
        cairo_pdf_surface_set_size(surface, w, h);

        poppler_page_render_for_printing(page, cr);

        char label[64];
        if (!strcmp(mode, "page_total"))
            snprintf(label, sizeof(label), "%d / %d", i + 1, n_pages);
        else
            snprintf(label, sizeof(label), "%d", i + 1);

        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 9);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, label, &ext);
        double x = (w - ext.width) / 2.0 - ext.x_bearing;
        double y = h - 22;  /* ~0.5cm higher than before */
        cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
        cairo_move_to(cr, x, y);
        cairo_show_text(cr, label);

        g_object_unref(page);
        cairo_show_page(cr);
    }

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    g_object_unref(doc);

    g_rename(tmp_path, pdf_path);
    g_free(tmp_path);
}

typedef struct {
    char *pdf_path;
    char  page_numbers[16];
    int   retries;
} PdfPostData;

static gboolean pdf_post_process_cb(gpointer data) {
    PdfPostData *pd = data;
    struct stat st;
    if (stat(pd->pdf_path, &st) == 0 && st.st_size > 0) {
        pdf_add_page_numbers(pd->pdf_path, pd->page_numbers);
        g_free(pd->pdf_path);
        g_free(pd);
        return G_SOURCE_REMOVE;
    }
    if (++pd->retries > 40) {  /* 20s max */
        g_free(pd->pdf_path);
        g_free(pd);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

void preview_export_pdf(NotesWindow *win, const char *out_path) {
    if (!win->preview_webview || !win->preview_ready) return;

    WebKitPrintOperation *op = webkit_print_operation_new(
        WEBKIT_WEB_VIEW(win->preview_webview));

    GtkPrintSettings *ps = gtk_print_settings_new();
    char *uri = g_filename_to_uri(out_path, NULL, NULL);
    if (uri) {
        gtk_print_settings_set(ps, GTK_PRINT_SETTINGS_OUTPUT_URI, uri);
        g_free(uri);
    }
    gtk_print_settings_set(ps, GTK_PRINT_SETTINGS_OUTPUT_FILE_FORMAT, "pdf");
    gtk_print_settings_set_printer(ps, "Print to File");

    GtkPageSetup *page = gtk_page_setup_new();
    if (win->settings.pdf_landscape)
        gtk_page_setup_set_orientation(page, GTK_PAGE_ORIENTATION_LANDSCAPE);
    else
        gtk_page_setup_set_orientation(page, GTK_PAGE_ORIENTATION_PORTRAIT);

    double bottom = win->settings.pdf_margin_bottom;
    if (strcmp(win->settings.pdf_page_numbers, "none") != 0)
        bottom += 5;  /* extra space for page number line */

    gtk_page_setup_set_top_margin(page, win->settings.pdf_margin_top, GTK_UNIT_MM);
    gtk_page_setup_set_bottom_margin(page, bottom, GTK_UNIT_MM);
    gtk_page_setup_set_left_margin(page, win->settings.pdf_margin_left, GTK_UNIT_MM);
    gtk_page_setup_set_right_margin(page, win->settings.pdf_margin_right, GTK_UNIT_MM);

    webkit_print_operation_set_print_settings(op, ps);
    webkit_print_operation_set_page_setup(op, page);

    webkit_print_operation_print(op);

    g_object_unref(ps);
    g_object_unref(page);
    g_object_unref(op);

    if (strcmp(win->settings.pdf_page_numbers, "none") != 0) {
        PdfPostData *pd = g_new0(PdfPostData, 1);
        pd->pdf_path = g_strdup(out_path);
        snprintf(pd->page_numbers, sizeof(pd->page_numbers), "%s",
                 win->settings.pdf_page_numbers);
        g_timeout_add(500, pdf_post_process_cb, pd);
    }
}

static void on_load_changed(WebKitWebView *wv, WebKitLoadEvent ev, gpointer user) {
    (void)wv;
    NotesWindow *win = user;
    if (ev == WEBKIT_LOAD_FINISHED) {
        win->preview_ready = TRUE;
        preview_apply_theme(win);
        preview_apply_layout(win);
        push_text(win);
    }
}

static void on_buffer_changed_preview(GtkTextBuffer *b, gpointer user) {
    (void)b;
    NotesWindow *win = user;
    if (win->preview_visible) preview_queue_update(win);
}

static void on_switch_page(GtkNotebook *nb, GtkWidget *page, guint idx, gpointer user) {
    (void)nb; (void)page;
    NotesWindow *win = user;
    gboolean vis = (idx == 1);
    win->preview_visible = vis;
    if (vis) {
        preview_apply_theme(win);
        preview_apply_layout(win);
        push_text(win);
    } else {
        gtk_widget_grab_focus(GTK_WIDGET(win->text_view));
    }
}

static gboolean on_preview_ctrl_scroll(GtkEventControllerScroll *ctrl,
                                        double dx, double dy, gpointer user) {
    (void)dx;
    GdkModifierType mods = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(ctrl));
    if (!(mods & GDK_CONTROL_MASK)) return FALSE;
    NotesWindow *win = user;
    if (dy < 0 && win->settings.preview_font_size < 72)
        win->settings.preview_font_size += 1;
    else if (dy > 0 && win->settings.preview_font_size > 6)
        win->settings.preview_font_size -= 1;
    else
        return TRUE;
    preview_apply_font_size(win);
    settings_save(&win->settings);
    return TRUE;
}

void preview_init(NotesWindow *win) {
    char *dir = resolve_webview_dir();
    if (!dir) {
        g_warning("notes-md: data/webview not found next to executable");
        return;
    }

    WebKitSettings *wset = webkit_settings_new();
    webkit_settings_set_enable_javascript(wset, TRUE);
    webkit_settings_set_allow_file_access_from_file_urls(wset, TRUE);
    webkit_settings_set_allow_universal_access_from_file_urls(wset, TRUE);
    webkit_settings_set_enable_write_console_messages_to_stdout(wset, TRUE);

    GtkWidget *wv = webkit_web_view_new();
    webkit_web_view_set_settings(WEBKIT_WEB_VIEW(wv), wset);
    g_object_unref(wset);
    g_signal_connect(wv, "load-changed", G_CALLBACK(on_load_changed), win);

    char *uri = g_strdup_printf("file://%s/preview.html", dir);
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(wv), uri);
    g_free(uri); g_free(dir);

    win->preview_webview = wv;
    win->preview_zoom = (double)win->settings.preview_font_size / 14.0;
    if (win->preview_zoom < 0.4) win->preview_zoom = 0.4;
    if (win->preview_zoom > 4.0) win->preview_zoom = 4.0;
    webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(wv), win->preview_zoom);

    /* Ctrl+scroll zoom on preview */
    GtkEventController *pv_scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    gtk_event_controller_set_propagation_phase(pv_scroll, GTK_PHASE_CAPTURE);
    g_signal_connect(pv_scroll, "scroll", G_CALLBACK(on_preview_ctrl_scroll), win);
    gtk_widget_add_controller(wv, pv_scroll);

    /* Find bar — hidden by default, shown on Ctrl+F in preview */
    GtkWidget *find_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(find_bar, 8);
    gtk_widget_set_margin_end(find_bar, 8);
    gtk_widget_set_margin_top(find_bar, 4);
    gtk_widget_set_margin_bottom(find_bar, 4);
    gtk_widget_set_visible(find_bar, FALSE);

    GtkWidget *find_entry = gtk_search_entry_new();
    gtk_widget_set_hexpand(find_entry, TRUE);
    GtkWidget *find_prev = gtk_button_new_from_icon_name("go-up-symbolic");
    GtkWidget *find_next = gtk_button_new_from_icon_name("go-down-symbolic");
    GtkWidget *find_close = gtk_button_new_from_icon_name("window-close-symbolic");
    GtkWidget *find_lbl = gtk_label_new("");
    gtk_widget_add_css_class(find_lbl, "dim-label");

    gtk_box_append(GTK_BOX(find_bar), find_entry);
    gtk_box_append(GTK_BOX(find_bar), find_prev);
    gtk_box_append(GTK_BOX(find_bar), find_next);
    gtk_box_append(GTK_BOX(find_bar), find_lbl);
    gtk_box_append(GTK_BOX(find_bar), find_close);

    g_signal_connect(find_entry, "changed", G_CALLBACK(on_find_entry_changed), win);
    g_signal_connect(find_prev, "clicked", G_CALLBACK(on_find_prev), win);
    g_signal_connect(find_next, "clicked", G_CALLBACK(on_find_next), win);
    g_signal_connect(find_close, "clicked", G_CALLBACK(on_find_close), win);

    GtkEventController *find_keys = gtk_event_controller_key_new();
    g_signal_connect(find_keys, "key-pressed", G_CALLBACK(on_find_entry_key), win);
    gtk_widget_add_controller(find_entry, find_keys);

    WebKitFindController *fc = webkit_web_view_get_find_controller(WEBKIT_WEB_VIEW(wv));
    g_signal_connect(fc, "counted-matches", G_CALLBACK(on_find_matches), win);
    g_signal_connect(fc, "failed-to-find-text", G_CALLBACK(on_find_failed), win);

    win->preview_find_bar = find_bar;
    win->preview_find_entry = find_entry;
    win->preview_find_label = GTK_LABEL(find_lbl);

    /* Wrap webview + find bar in a vertical box */
    GtkWidget *preview_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(preview_page, TRUE);
    gtk_widget_set_hexpand(preview_page, TRUE);
    gtk_box_append(GTK_BOX(preview_page), find_bar);
    gtk_box_append(GTK_BOX(preview_page), wv);

    /* Swap editor_box for a GtkNotebook with two tabs (source / preview). */
    GtkWidget *parent = gtk_widget_get_parent(win->editor_box);
    g_object_ref(win->editor_box);
    gtk_box_remove(GTK_BOX(parent), win->editor_box);

    win->preview_stack = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(win->preview_stack), GTK_POS_TOP);
    gtk_widget_set_vexpand(win->preview_stack, TRUE);
    gtk_widget_set_hexpand(win->preview_stack, TRUE);

    gtk_widget_set_vexpand(win->editor_box, TRUE);
    gtk_widget_set_hexpand(win->editor_box, TRUE);
    gtk_widget_set_vexpand(wv, TRUE);
    gtk_widget_set_hexpand(wv, TRUE);

    gtk_notebook_append_page(GTK_NOTEBOOK(win->preview_stack),
                             win->editor_box, gtk_label_new("Source"));
    gtk_notebook_append_page(GTK_NOTEBOOK(win->preview_stack),
                             preview_page, gtk_label_new("Preview"));
    g_object_unref(win->editor_box);

    win->preview_switcher = NULL;

    /* Temporarily detach status bar (last child), append stack, re-append status bar. */
    GtkWidget *status = gtk_widget_get_last_child(parent);
    if (status) {
        g_object_ref(status);
        gtk_box_remove(GTK_BOX(parent), status);
    }
    gtk_box_append(GTK_BOX(parent), win->preview_stack);
    if (status) {
        gtk_box_append(GTK_BOX(parent), status);
        g_object_unref(status);
    }

    gtk_notebook_set_current_page(GTK_NOTEBOOK(win->preview_stack), 0);
    win->preview_visible = FALSE;

    g_signal_connect(win->preview_stack, "switch-page",
                     G_CALLBACK(on_switch_page), win);

    g_signal_connect_after(win->buffer, "changed",
                           G_CALLBACK(on_buffer_changed_preview), win);
}

gboolean preview_is_visible(NotesWindow *win) {
    return win->preview_visible;
}

void preview_toggle(NotesWindow *win) {
    if (!win->preview_stack) return;
    int cur = gtk_notebook_get_current_page(GTK_NOTEBOOK(win->preview_stack));
    gtk_notebook_set_current_page(GTK_NOTEBOOK(win->preview_stack), cur == 1 ? 0 : 1);
}

void preview_zoom_step(NotesWindow *win, double delta) {
    if (!win->preview_webview) return;
    double z = win->preview_zoom + delta;
    if (z < 0.4) z = 0.4;
    if (z > 4.0) z = 4.0;
    win->preview_zoom = z;
    webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(win->preview_webview), z);
}
