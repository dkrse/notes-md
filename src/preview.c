#define _GNU_SOURCE
#include "preview.h"
#include <adwaita.h>
#include <webkit/webkit.h>
#include <string.h>
#include <unistd.h>

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
    preview_zoom_step(win, dy < 0 ? 0.1 : -0.1);
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
    win->preview_zoom = 1.0;
    webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(wv), 1.0);

    /* Ctrl+scroll zoom on preview */
    GtkEventController *pv_scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    gtk_event_controller_set_propagation_phase(pv_scroll, GTK_PHASE_CAPTURE);
    g_signal_connect(pv_scroll, "scroll", G_CALLBACK(on_preview_ctrl_scroll), win);
    gtk_widget_add_controller(wv, pv_scroll);

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
                             wv, gtk_label_new("Preview"));
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
