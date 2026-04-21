#include "actions.h"
#include "preview.h"
#include "ssh.h"
#include <adwaita.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

/* --- Theme list --- */
static const char *theme_ids[] = {
    "system", "light", "dark",
    "solarized-light", "solarized-dark",
    "monokai",
    "gruvbox-light", "gruvbox-dark",
    "nord", "dracula", "tokyo-night",
    "catppuccin-latte", "catppuccin-mocha",
    NULL
};
static const char *theme_labels[] = {
    "System", "Light", "Dark",
    "Solarized Light", "Solarized Dark",
    "Monokai",
    "Gruvbox Light", "Gruvbox Dark",
    "Nord", "Dracula", "Tokyo Night",
    "Catppuccin Latte", "Catppuccin Mocha",
    NULL
};

static int theme_index_of(const char *id) {
    for (int i = 0; theme_ids[i]; i++)
        if (strcmp(theme_ids[i], id) == 0) return i;
    return 0;
}

/* --- Actions --- */

static void on_new_file(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    g_free(win->original_content);
    win->original_content = g_strdup("");
    win->original_hash = fnv1a_hash("", 0);
    gtk_text_buffer_set_text(win->buffer, "", -1);
    win->dirty = FALSE;
    win->is_binary = FALSE;
    win->is_truncated = FALSE;
    gtk_window_set_title(GTK_WINDOW(win->window), "Notes MD");
    gtk_label_set_text(win->status_encoding, "UTF-8");
    win->current_file[0] = '\0';
    win->settings.last_file[0] = '\0';
    settings_save(&win->settings);
    gtk_widget_grab_focus(GTK_WIDGET(win->text_view));
}

static void on_save(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    if (!win->dirty) return;
    if (win->is_truncated || win->is_binary) {
        g_action_group_activate_action(G_ACTION_GROUP(win->window), "save-as", NULL);
        return;
    }

    /* Remote save */
    if (notes_window_is_remote(win) && ssh_path_is_remote(win->current_file)) {
        save_remote_file(win);
        return;
    }

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(win->buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(win->buffer, &start, &end, FALSE);

    if (win->current_file[0] != '\0') {
        char tmp[2112];
        snprintf(tmp, sizeof(tmp), "%s.XXXXXX", win->current_file);
        int fd = g_mkstemp(tmp);
        if (fd >= 0) {
            FILE *f = fdopen(fd, "w");
            if (f) {
                gboolean ok = (fputs(text, f) != EOF);
                ok = ok && (fflush(f) == 0);
                fclose(f);
                if (ok)
                    g_rename(tmp, win->current_file);
                else
                    g_remove(tmp);
            } else {
                close(fd);
                g_remove(tmp);
            }
        }
    } else {
        g_free(text);
        g_action_group_activate_action(G_ACTION_GROUP(win->window), "save-as", NULL);
        return;
    }
    g_free(win->original_content);
    win->original_content = text;
    win->original_hash = fnv1a_hash(text, strlen(text));
    win->dirty = FALSE;
    win->is_binary = FALSE;

    char *base = g_path_get_basename(win->current_file);
    gtk_window_set_title(GTK_WINDOW(win->window), base);
    g_free(base);
}

static void on_save_as_cb(GObject *source, GAsyncResult *result, gpointer data) {
    NotesWindow *win = data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GFile *file = gtk_file_dialog_save_finish(dialog, result, NULL);
    if (file) {
        char *path = g_file_get_path(file);
        if (path) {
            GtkTextIter start, end;
            gtk_text_buffer_get_bounds(win->buffer, &start, &end);
            char *text = gtk_text_buffer_get_text(win->buffer, &start, &end, FALSE);

            /* Atomic write with exclusive tmp */
            char tmp[2112];
            snprintf(tmp, sizeof(tmp), "%s.XXXXXX", path);
            int fd = g_mkstemp(tmp);
            if (fd >= 0) {
                FILE *f = fdopen(fd, "w");
                if (f) {
                    gboolean ok = (fputs(text, f) != EOF);
                    ok = ok && (fflush(f) == 0);
                    fclose(f);
                    if (ok)
                        g_rename(tmp, path);
                    else
                        g_remove(tmp);
                } else {
                    close(fd);
                    g_remove(tmp);
                }
            }

            snprintf(win->current_file, sizeof(win->current_file), "%s", path);
            snprintf(win->settings.last_file, sizeof(win->settings.last_file), "%s", path);
            g_free(win->original_content);
            win->original_content = text;
            win->original_hash = fnv1a_hash(text, strlen(text));
            win->dirty = FALSE;
            settings_save(&win->settings);

            char *base = g_path_get_basename(path);
            gtk_window_set_title(GTK_WINDOW(win->window), base);
            g_free(base);
            g_free(path);
        }
        g_object_unref(file);
    }
    g_object_unref(dialog);
}

static void on_save_as(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Save As");

    gtk_file_dialog_save(dialog, GTK_WINDOW(win->window), NULL, on_save_as_cb, win);
}

static void on_open_file_cb(GObject *source, GAsyncResult *result, gpointer data) {
    NotesWindow *win = data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GFile *file = gtk_file_dialog_open_finish(dialog, result, NULL);
    if (file) {
        char *path = g_file_get_path(file);
        if (path) {
            notes_window_load_file(win, path);
            g_free(path);
        }
        g_object_unref(file);
    }
    g_object_unref(dialog);
}

static void on_open_file(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Open File");
    gtk_file_dialog_open(dialog, GTK_WINDOW(win->window), NULL, on_open_file_cb, win);
}

static void on_find(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    if (preview_is_visible(win)) {
        preview_find_show(win);
        return;
    }
    notes_window_show_search(win, FALSE);
}

static void on_find_replace(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    notes_window_show_search(data, TRUE);
}

static void on_export_pdf_cb(GObject *src, GAsyncResult *res, gpointer data) {
    NotesWindow *win = data;
    GFile *f = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, NULL);
    if (!f) return;
    char *path = g_file_get_path(f);
    if (path) {
        preview_export_pdf(win, path);
        g_free(path);
    }
    g_object_unref(f);
}

static void on_export_pdf(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    if (!win->preview_webview) return;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Export to PDF");
    /* Suggest filename based on current file */
    if (win->current_file[0]) {
        char *base = g_path_get_basename(win->current_file);
        char *dot = strrchr(base, '.');
        if (dot) *dot = '\0';
        char *suggested = g_strdup_printf("%s.pdf", base);
        gtk_file_dialog_set_initial_name(dialog, suggested);
        g_free(suggested);
        g_free(base);
    } else {
        gtk_file_dialog_set_initial_name(dialog, "document.pdf");
    }
    gtk_file_dialog_save(dialog, GTK_WINDOW(win->window), NULL, on_export_pdf_cb, win);
}

static void on_goto_line(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    notes_window_goto_line(data);
}

static void on_zoom_in(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    if (preview_is_visible(win)) {
        preview_zoom_step(win, 0.1);
        return;
    }
    if (win->settings.font_size < 72) {
        win->settings.font_size += 2;
        notes_window_apply_settings(win);
        settings_save(&win->settings);
    }
}

static void on_zoom_out(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    if (preview_is_visible(win)) {
        preview_zoom_step(win, -0.1);
        return;
    }
    if (win->settings.font_size > 6) {
        win->settings.font_size -= 2;
        notes_window_apply_settings(win);
        settings_save(&win->settings);
    }
}

/* --- Settings dialog --- */

static void on_settings_apply(GtkButton *button, gpointer data) {
    NotesWindow *win = data;
    settings_save(&win->settings);
    notes_window_apply_settings(win);
    GtkWidget *toplevel = GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(button)));
    gtk_window_destroy(GTK_WINDOW(toplevel));
}

static void on_settings_cancel(GtkButton *button, gpointer data) {
    (void)data;
    GtkWidget *toplevel = GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(button)));
    gtk_window_destroy(GTK_WINDOW(toplevel));
}

static void on_theme_changed(GtkDropDown *dropdown, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    NotesWindow *win = data;
    guint idx = gtk_drop_down_get_selected(dropdown);
    if (theme_ids[idx])
        snprintf(win->settings.theme, sizeof(win->settings.theme), "%s", theme_ids[idx]);
}

static void on_spacing_changed(GtkDropDown *dropdown, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    NotesWindow *win = data;
    guint idx = gtk_drop_down_get_selected(dropdown);
    double spacings[] = {1.0, 1.2, 1.5, 2.0};
    if (idx < 4)
        win->settings.line_spacing = spacings[idx];
}

static void on_line_numbers_toggled(GtkCheckButton *btn, gpointer data) {
    NotesWindow *win = data;
    win->settings.show_line_numbers = gtk_check_button_get_active(btn);
}

static void on_highlight_line_toggled(GtkCheckButton *btn, gpointer data) {
    NotesWindow *win = data;
    win->settings.highlight_current_line = gtk_check_button_get_active(btn);
}

static void on_wrap_lines_toggled(GtkCheckButton *btn, gpointer data) {
    NotesWindow *win = data;
    win->settings.wrap_lines = gtk_check_button_get_active(btn);
}

static void on_highlight_syntax_toggled(GtkCheckButton *btn, gpointer data) {
    NotesWindow *win = data;
    win->settings.highlight_syntax = gtk_check_button_get_active(btn);
}

static void on_preview_full_width_toggled(GtkCheckButton *btn, gpointer data) {
    NotesWindow *win = data;
    win->settings.preview_full_width = gtk_check_button_get_active(btn);
}

static void on_watch_file_toggled(GtkCheckButton *btn, gpointer data) {
    NotesWindow *win = data;
    win->settings.watch_file = gtk_check_button_get_active(btn);
}

static void on_disable_gpu_toggled(GtkCheckButton *btn, gpointer data) {
    NotesWindow *win = data;
    win->settings.disable_gpu = gtk_check_button_get_active(btn);
}

static void on_math_engine_changed(GtkDropDown *dd, GParamSpec *p, gpointer data) {
    (void)p;
    NotesWindow *win = data;
    static const char *engines[] = {"katex", "mathjax"};
    guint idx = gtk_drop_down_get_selected(dd);
    if (idx < 2)
        snprintf(win->settings.math_engine, sizeof(win->settings.math_engine),
                 "%s", engines[idx]);
}

static void on_preview_font_size_changed(GtkSpinButton *btn, gpointer data) {
    NotesWindow *win = data;
    win->settings.preview_font_size = (int)gtk_spin_button_get_value(btn);
}

static void on_pdf_margin_top(GtkSpinButton *btn, gpointer data) {
    NotesWindow *win = data; win->settings.pdf_margin_top = gtk_spin_button_get_value(btn);
}
static void on_pdf_margin_bottom(GtkSpinButton *btn, gpointer data) {
    NotesWindow *win = data; win->settings.pdf_margin_bottom = gtk_spin_button_get_value(btn);
}
static void on_pdf_margin_left(GtkSpinButton *btn, gpointer data) {
    NotesWindow *win = data; win->settings.pdf_margin_left = gtk_spin_button_get_value(btn);
}
static void on_pdf_margin_right(GtkSpinButton *btn, gpointer data) {
    NotesWindow *win = data; win->settings.pdf_margin_right = gtk_spin_button_get_value(btn);
}
static void on_pdf_landscape_toggled(GtkCheckButton *btn, gpointer data) {
    NotesWindow *win = data; win->settings.pdf_landscape = gtk_check_button_get_active(btn);
}
static void on_pdf_page_numbers_changed(GtkDropDown *dd, GParamSpec *p, gpointer data) {
    (void)p;
    NotesWindow *win = data;
    static const char *opts[] = {"none", "page", "page_total"};
    guint idx = gtk_drop_down_get_selected(dd);
    if (idx < 3)
        snprintf(win->settings.pdf_page_numbers, sizeof(win->settings.pdf_page_numbers),
                 "%s", opts[idx]);
}


static void on_intensity_changed(GtkRange *range, gpointer data) {
    NotesWindow *win = data;
    win->settings.font_intensity = gtk_range_get_value(range);
}

static void on_font_set(GtkFontDialogButton *button, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    NotesWindow *win = data;
    PangoFontDescription *desc = gtk_font_dialog_button_get_font_desc(button);
    if (desc) {
        const char *family = pango_font_description_get_family(desc);
        if (family)
            snprintf(win->settings.font, sizeof(win->settings.font), "%s", family);
        int size = pango_font_description_get_size(desc);
        if (size > 0)
            win->settings.font_size = size / PANGO_SCALE;
    }
}

static void on_gui_font_set(GtkFontDialogButton *button, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    NotesWindow *win = data;
    PangoFontDescription *desc = gtk_font_dialog_button_get_font_desc(button);
    if (desc) {
        const char *family = pango_font_description_get_family(desc);
        if (family)
            snprintf(win->settings.gui_font, sizeof(win->settings.gui_font), "%s", family);
        int size = pango_font_description_get_size(desc);
        if (size > 0)
            win->settings.gui_font_size = size / PANGO_SCALE;
    }
}

static void on_settings(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Settings");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 420, -1);
    gtk_window_set_titlebar(GTK_WINDOW(dialog), adw_header_bar_new());

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox, 16);
    gtk_widget_set_margin_end(vbox, 16);
    gtk_widget_set_margin_top(vbox, 16);
    gtk_widget_set_margin_bottom(vbox, 16);
    gtk_window_set_child(GTK_WINDOW(dialog), vbox);

    /* Tab switcher + stack */
    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    GtkWidget *switcher = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(stack));
    gtk_widget_set_halign(switcher, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(vbox), switcher);
    gtk_box_append(GTK_BOX(vbox), stack);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_stack_add_titled(GTK_STACK(stack), grid, "editor", "Editor");

    int row = 0;

    /* Theme */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Theme:"), 0, row, 1, 1);
    GtkWidget *theme_dd = gtk_drop_down_new_from_strings(theme_labels);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(theme_dd), (guint)theme_index_of(win->settings.theme));
    g_signal_connect(theme_dd, "notify::selected", G_CALLBACK(on_theme_changed), win);
    gtk_grid_attach(GTK_GRID(grid), theme_dd, 1, row++, 1, 1);

    /* Font */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Font:"), 0, row, 1, 1);
    PangoFontDescription *desc = pango_font_description_new();
    pango_font_description_set_family(desc, win->settings.font);
    pango_font_description_set_size(desc, win->settings.font_size * PANGO_SCALE);
    GtkFontDialog *font_dialog = gtk_font_dialog_new();
    GtkWidget *font_btn = gtk_font_dialog_button_new(font_dialog);
    gtk_font_dialog_button_set_font_desc(GTK_FONT_DIALOG_BUTTON(font_btn), desc);
    pango_font_description_free(desc);
    g_signal_connect(font_btn, "notify::font-desc", G_CALLBACK(on_font_set), win);
    gtk_grid_attach(GTK_GRID(grid), font_btn, 1, row++, 1, 1);

    /* GUI font */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("GUI Font:"), 0, row, 1, 1);
    PangoFontDescription *gui_desc = pango_font_description_new();
    pango_font_description_set_family(gui_desc, win->settings.gui_font);
    pango_font_description_set_size(gui_desc, win->settings.gui_font_size * PANGO_SCALE);
    GtkFontDialog *gui_font_dialog = gtk_font_dialog_new();
    GtkWidget *gui_font_btn = gtk_font_dialog_button_new(gui_font_dialog);
    gtk_font_dialog_button_set_font_desc(GTK_FONT_DIALOG_BUTTON(gui_font_btn), gui_desc);
    pango_font_description_free(gui_desc);
    g_signal_connect(gui_font_btn, "notify::font-desc", G_CALLBACK(on_gui_font_set), win);
    gtk_grid_attach(GTK_GRID(grid), gui_font_btn, 1, row++, 1, 1);

    /* Font intensity */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Font Intensity:"), 0, row, 1, 1);
    GtkWidget *intensity_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.3, 1.0, 0.01);
    gtk_range_set_value(GTK_RANGE(intensity_scale), win->settings.font_intensity);
    g_signal_connect(intensity_scale, "value-changed", G_CALLBACK(on_intensity_changed), win);
    gtk_grid_attach(GTK_GRID(grid), intensity_scale, 1, row++, 1, 1);

    /* Line spacing */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Line Spacing:"), 0, row, 1, 1);
    const char *spacings[] = {"1", "1.2", "1.5", "2", NULL};
    GtkWidget *sp_dd = gtk_drop_down_new_from_strings(spacings);
    guint sp_idx = 0;
    if (win->settings.line_spacing >= 1.9) sp_idx = 3;
    else if (win->settings.line_spacing >= 1.4) sp_idx = 2;
    else if (win->settings.line_spacing >= 1.1) sp_idx = 1;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(sp_dd), sp_idx);
    g_signal_connect(sp_dd, "notify::selected", G_CALLBACK(on_spacing_changed), win);
    gtk_grid_attach(GTK_GRID(grid), sp_dd, 1, row++, 1, 1);

    /* Line numbers */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Line Numbers:"), 0, row, 1, 1);
    GtkWidget *ln_check = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ln_check), win->settings.show_line_numbers);
    g_signal_connect(ln_check, "toggled", G_CALLBACK(on_line_numbers_toggled), win);
    gtk_grid_attach(GTK_GRID(grid), ln_check, 1, row++, 1, 1);

    /* Highlight current line */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Highlight Line:"), 0, row, 1, 1);
    GtkWidget *hl_check = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(hl_check), win->settings.highlight_current_line);
    g_signal_connect(hl_check, "toggled", G_CALLBACK(on_highlight_line_toggled), win);
    gtk_grid_attach(GTK_GRID(grid), hl_check, 1, row++, 1, 1);

    /* Wrap lines */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Wrap Lines:"), 0, row, 1, 1);
    GtkWidget *wrap_check = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(wrap_check), win->settings.wrap_lines);
    g_signal_connect(wrap_check, "toggled", G_CALLBACK(on_wrap_lines_toggled), win);
    gtk_grid_attach(GTK_GRID(grid), wrap_check, 1, row++, 1, 1);

    /* Syntax highlighting */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Syntax Highlight:"), 0, row, 1, 1);
    GtkWidget *hl_syntax_check = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(hl_syntax_check), win->settings.highlight_syntax);
    g_signal_connect(hl_syntax_check, "toggled", G_CALLBACK(on_highlight_syntax_toggled), win);
    gtk_grid_attach(GTK_GRID(grid), hl_syntax_check, 1, row++, 1, 1);

    /* Preview full width (left-aligned instead of centered) */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Preview Full Width:"), 0, row, 1, 1);
    GtkWidget *pfw_check = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(pfw_check), win->settings.preview_full_width);
    g_signal_connect(pfw_check, "toggled", G_CALLBACK(on_preview_full_width_toggled), win);
    gtk_grid_attach(GTK_GRID(grid), pfw_check, 1, row++, 1, 1);

    /* Preview font size */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Preview Font Size:"), 0, row, 1, 1);
    GtkWidget *pvfs_spin = gtk_spin_button_new_with_range(6, 72, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pvfs_spin), win->settings.preview_font_size);
    g_signal_connect(pvfs_spin, "value-changed", G_CALLBACK(on_preview_font_size_changed), win);
    gtk_grid_attach(GTK_GRID(grid), pvfs_spin, 1, row++, 1, 1);

    /* Watch file for external changes */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Reload on File Change:"), 0, row, 1, 1);
    GtkWidget *wf_check = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(wf_check), win->settings.watch_file);
    g_signal_connect(wf_check, "toggled", G_CALLBACK(on_watch_file_toggled), win);
    gtk_grid_attach(GTK_GRID(grid), wf_check, 1, row++, 1, 1);

    /* Disable GPU compositing (workaround for broken drivers; requires restart to apply) */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Disable Preview GPU:"), 0, row, 1, 1);
    GtkWidget *dg_check = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(dg_check), win->settings.disable_gpu);
    g_signal_connect(dg_check, "toggled", G_CALLBACK(on_disable_gpu_toggled), win);
    gtk_grid_attach(GTK_GRID(grid), dg_check, 1, row++, 1, 1);

    /* Math engine (applies on restart) */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Math Engine:"), 0, row, 1, 1);
    const char *engine_labels[] = {"KaTeX (fast, sync)", "MathJax (richer LaTeX)", NULL};
    GtkWidget *me_dd = gtk_drop_down_new_from_strings(engine_labels);
    guint me_idx = (strcmp(win->settings.math_engine, "mathjax") == 0) ? 1 : 0;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(me_dd), me_idx);
    g_signal_connect(me_dd, "notify::selected", G_CALLBACK(on_math_engine_changed), win);
    gtk_grid_attach(GTK_GRID(grid), me_dd, 1, row++, 1, 1);

    /* ── PDF tab ── */
    GtkWidget *pdf_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(pdf_grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(pdf_grid), 12);
    gtk_stack_add_titled(GTK_STACK(stack), pdf_grid, "pdf", "PDF");
    int prow = 0;

    gtk_grid_attach(GTK_GRID(pdf_grid), gtk_label_new("Margin Top (mm):"), 0, prow, 1, 1);
    GtkWidget *mt = gtk_spin_button_new_with_range(0, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(mt), win->settings.pdf_margin_top);
    g_signal_connect(mt, "value-changed", G_CALLBACK(on_pdf_margin_top), win);
    gtk_grid_attach(GTK_GRID(pdf_grid), mt, 1, prow++, 1, 1);

    gtk_grid_attach(GTK_GRID(pdf_grid), gtk_label_new("Margin Bottom (mm):"), 0, prow, 1, 1);
    GtkWidget *mb = gtk_spin_button_new_with_range(0, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(mb), win->settings.pdf_margin_bottom);
    g_signal_connect(mb, "value-changed", G_CALLBACK(on_pdf_margin_bottom), win);
    gtk_grid_attach(GTK_GRID(pdf_grid), mb, 1, prow++, 1, 1);

    gtk_grid_attach(GTK_GRID(pdf_grid), gtk_label_new("Margin Left (mm):"), 0, prow, 1, 1);
    GtkWidget *ml = gtk_spin_button_new_with_range(0, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ml), win->settings.pdf_margin_left);
    g_signal_connect(ml, "value-changed", G_CALLBACK(on_pdf_margin_left), win);
    gtk_grid_attach(GTK_GRID(pdf_grid), ml, 1, prow++, 1, 1);

    gtk_grid_attach(GTK_GRID(pdf_grid), gtk_label_new("Margin Right (mm):"), 0, prow, 1, 1);
    GtkWidget *mr = gtk_spin_button_new_with_range(0, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(mr), win->settings.pdf_margin_right);
    g_signal_connect(mr, "value-changed", G_CALLBACK(on_pdf_margin_right), win);
    gtk_grid_attach(GTK_GRID(pdf_grid), mr, 1, prow++, 1, 1);

    gtk_grid_attach(GTK_GRID(pdf_grid), gtk_label_new("Landscape:"), 0, prow, 1, 1);
    GtkWidget *ls_check = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ls_check), win->settings.pdf_landscape);
    g_signal_connect(ls_check, "toggled", G_CALLBACK(on_pdf_landscape_toggled), win);
    gtk_grid_attach(GTK_GRID(pdf_grid), ls_check, 1, prow++, 1, 1);

    gtk_grid_attach(GTK_GRID(pdf_grid), gtk_label_new("Page Numbers:"), 0, prow, 1, 1);
    const char *pn_labels[] = {"None", "Page N", "Page N of M", NULL};
    GtkWidget *pn_dd = gtk_drop_down_new_from_strings(pn_labels);
    guint pn_idx = 0;
    if (!strcmp(win->settings.pdf_page_numbers, "page")) pn_idx = 1;
    else if (!strcmp(win->settings.pdf_page_numbers, "page_total")) pn_idx = 2;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(pn_dd), pn_idx);
    g_signal_connect(pn_dd, "notify::selected", G_CALLBACK(on_pdf_page_numbers_changed), win);
    gtk_grid_attach(GTK_GRID(pdf_grid), pn_dd, 1, prow++, 1, 1);

    /* Buttons */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_END);
    GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
    GtkWidget *apply_btn = gtk_button_new_with_label("Apply");
    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_settings_cancel), win);
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_settings_apply), win);
    gtk_box_append(GTK_BOX(btn_box), cancel_btn);
    gtk_box_append(GTK_BOX(btn_box), apply_btn);
    gtk_box_append(GTK_BOX(vbox), btn_box);

    gtk_window_present(GTK_WINDOW(dialog));
}

/* ── SFTP/SSH Connection Dialog ── */

static GtkWidget *make_label(const char *text) {
    GtkWidget *lbl = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(lbl), 1.0);
    return lbl;
}

typedef struct {
    NotesWindow     *win;
    GtkWindow       *dialog;
    SftpConnections  conns;
    GtkListBox      *conn_list;
    GtkEntry        *name_entry;
    GtkEntry        *host_entry;
    GtkEntry        *port_entry;
    GtkEntry        *user_entry;
    GtkEntry        *path_entry;
    GtkCheckButton  *use_key_check;
    GtkEntry        *key_entry;
    GtkWidget       *key_browse_btn;
    GtkWidget       *key_row;
    GtkWidget       *key_btn_row;
    int              selected_idx;
    int              ref_count;       /* prevent use-after-free during async connect */
    gboolean         dialog_alive;    /* FALSE after dialog destroy */
} SftpCtx;

static SftpCtx *sftp_ctx_ref(SftpCtx *ctx) {
    ctx->ref_count++;
    return ctx;
}

static void sftp_ctx_unref(SftpCtx *ctx) {
    if (--ctx->ref_count <= 0)
        g_free(ctx);
}

static void sftp_populate_list(SftpCtx *ctx) {
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(ctx->conn_list))))
        gtk_list_box_remove(ctx->conn_list, child);
    for (int i = 0; i < ctx->conns.count; i++) {
        GtkWidget *lbl = gtk_label_new(ctx->conns.items[i].name);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0);
        gtk_widget_set_margin_start(lbl, 8);
        gtk_widget_set_margin_end(lbl, 8);
        gtk_widget_set_margin_top(lbl, 4);
        gtk_widget_set_margin_bottom(lbl, 4);
        gtk_list_box_append(ctx->conn_list, lbl);
    }
}

static void sftp_update_auth_visibility(SftpCtx *ctx) {
    gboolean use_key = gtk_check_button_get_active(ctx->use_key_check);
    gtk_widget_set_visible(ctx->key_row, use_key);
    gtk_widget_set_visible(GTK_WIDGET(ctx->key_entry), use_key);
    gtk_widget_set_visible(ctx->key_btn_row, use_key);
}

static void on_use_key_toggled(GtkCheckButton *btn, gpointer data) {
    (void)btn;
    sftp_update_auth_visibility(data);
}

static void on_key_file_selected(GObject *src, GAsyncResult *res, gpointer data) {
    SftpCtx *ctx = data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(src);
    GFile *file = gtk_file_dialog_open_finish(dialog, res, NULL);
    if (file) {
        char *path = g_file_get_path(file);
        if (path) {
            gtk_editable_set_text(GTK_EDITABLE(ctx->key_entry), path);
            g_free(path);
        }
        g_object_unref(file);
    }
}

static void on_key_browse(GtkButton *btn, gpointer data) {
    (void)btn;
    SftpCtx *ctx = data;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select Private Key");
    char ssh_dir[1024];
    snprintf(ssh_dir, sizeof(ssh_dir), "%s/.ssh", g_get_home_dir());
    GFile *init = g_file_new_for_path(ssh_dir);
    gtk_file_dialog_set_initial_folder(dialog, init);
    g_object_unref(init);
    gtk_file_dialog_open(dialog, ctx->dialog, NULL, on_key_file_selected, ctx);
}

static void on_conn_selected(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box;
    SftpCtx *ctx = data;
    if (!row) { ctx->selected_idx = -1; return; }
    int idx = gtk_list_box_row_get_index(row);
    if (idx < 0 || idx >= ctx->conns.count) return;
    ctx->selected_idx = idx;
    SftpConnection *c = &ctx->conns.items[idx];
    gtk_editable_set_text(GTK_EDITABLE(ctx->name_entry), c->name);
    gtk_editable_set_text(GTK_EDITABLE(ctx->host_entry), c->host);
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", c->port);
    gtk_editable_set_text(GTK_EDITABLE(ctx->port_entry), port_str);
    gtk_editable_set_text(GTK_EDITABLE(ctx->user_entry), c->user);
    gtk_editable_set_text(GTK_EDITABLE(ctx->path_entry), c->remote_path);
    gtk_check_button_set_active(ctx->use_key_check, c->use_key);
    gtk_editable_set_text(GTK_EDITABLE(ctx->key_entry), c->key_path);
    sftp_update_auth_visibility(ctx);
}

static void sftp_save_form_to_conn(SftpCtx *ctx, SftpConnection *c) {
    g_strlcpy(c->name, gtk_editable_get_text(GTK_EDITABLE(ctx->name_entry)), sizeof(c->name));
    g_strlcpy(c->host, gtk_editable_get_text(GTK_EDITABLE(ctx->host_entry)), sizeof(c->host));
    c->port = atoi(gtk_editable_get_text(GTK_EDITABLE(ctx->port_entry)));
    if (c->port <= 0) c->port = 22;
    g_strlcpy(c->user, gtk_editable_get_text(GTK_EDITABLE(ctx->user_entry)), sizeof(c->user));
    g_strlcpy(c->remote_path, gtk_editable_get_text(GTK_EDITABLE(ctx->path_entry)), sizeof(c->remote_path));
    c->use_key = gtk_check_button_get_active(ctx->use_key_check);
    g_strlcpy(c->key_path, gtk_editable_get_text(GTK_EDITABLE(ctx->key_entry)), sizeof(c->key_path));
}

static void on_sftp_save(GtkButton *btn, gpointer data) {
    (void)btn;
    SftpCtx *ctx = data;
    const char *name = gtk_editable_get_text(GTK_EDITABLE(ctx->name_entry));
    if (!name[0]) return;
    if (ctx->selected_idx >= 0 && ctx->selected_idx < ctx->conns.count) {
        sftp_save_form_to_conn(ctx, &ctx->conns.items[ctx->selected_idx]);
    } else if (ctx->conns.count < MAX_CONNECTIONS) {
        int idx = ctx->conns.count++;
        memset(&ctx->conns.items[idx], 0, sizeof(SftpConnection));
        sftp_save_form_to_conn(ctx, &ctx->conns.items[idx]);
        ctx->selected_idx = idx;
    }
    connections_save(&ctx->conns);
    sftp_populate_list(ctx);
}

static void sftp_clear_form(SftpCtx *ctx);

static void on_sftp_delete(GtkButton *btn, gpointer data) {
    (void)btn;
    SftpCtx *ctx = data;
    if (ctx->selected_idx < 0 || ctx->selected_idx >= ctx->conns.count) return;
    for (int i = ctx->selected_idx; i < ctx->conns.count - 1; i++)
        ctx->conns.items[i] = ctx->conns.items[i + 1];
    ctx->conns.count--;
    connections_save(&ctx->conns);
    sftp_populate_list(ctx);
    sftp_clear_form(ctx);
}

static void sftp_clear_form(SftpCtx *ctx) {
    ctx->selected_idx = -1;
    gtk_list_box_unselect_all(ctx->conn_list);
    gtk_editable_set_text(GTK_EDITABLE(ctx->name_entry), "");
    gtk_editable_set_text(GTK_EDITABLE(ctx->host_entry), "");
    gtk_editable_set_text(GTK_EDITABLE(ctx->port_entry), "22");
    gtk_editable_set_text(GTK_EDITABLE(ctx->user_entry), "");
    gtk_editable_set_text(GTK_EDITABLE(ctx->path_entry), "/");
    gtk_editable_set_text(GTK_EDITABLE(ctx->key_entry), "");
    gtk_check_button_set_active(ctx->use_key_check, FALSE);
    sftp_update_auth_visibility(ctx);
    gtk_widget_grab_focus(GTK_WIDGET(ctx->name_entry));
}

static void on_sftp_new(GtkButton *btn, gpointer data) {
    (void)btn;
    sftp_clear_form(data);
}

/* Async SSH connect */

typedef struct {
    SftpCtx    *ctx;
    GPtrArray  *argv;
    char        host[256];
    char        user[128];
    int         port;
    char        key[1024];
    char        remote[1024];
    GtkWidget  *connect_btn;
} ConnectTaskData;

static void connect_task_data_free(gpointer p) {
    ConnectTaskData *d = p;
    if (d->argv) g_ptr_array_unref(d->argv);
    g_free(d);
}

static void ssh_connect_thread(GTask *task, gpointer src, gpointer data,
                                GCancellable *cancel) {
    (void)src; (void)cancel;
    ConnectTaskData *d = data;
    g_ptr_array_add(d->argv, NULL);

    char *stdout_buf = NULL;
    char *stderr_buf = NULL;
    GError *err = NULL;
    gint status = 0;
    gboolean ok = g_spawn_sync(
        NULL, (char **)d->argv->pdata, NULL,
        G_SPAWN_SEARCH_PATH,
        NULL, NULL, &stdout_buf, &stderr_buf, &status, &err);
    g_free(stdout_buf);

    if (!ok) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED,
            "SSH failed: %s", err ? err->message : "unknown");
        if (err) g_error_free(err);
        g_free(stderr_buf);
        return;
    }
    if (!g_spawn_check_wait_status(status, NULL)) {
        int code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
        /* Trim trailing whitespace from stderr for cleaner display */
        if (stderr_buf) {
            g_strchomp(stderr_buf);
        }
        const char *msg = (stderr_buf && stderr_buf[0]) ? stderr_buf : "(no stderr output)";
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED,
            "SSH connection failed (exit %d).\n\n%s", code, msg);
        g_free(stderr_buf);
        return;
    }
    g_free(stderr_buf);
    g_task_return_boolean(task, TRUE);
}

static void ssh_connect_done(GObject *src, GAsyncResult *res, gpointer data) {
    (void)src;
    ConnectTaskData *d = data;
    SftpCtx *ctx = d->ctx;
    GError *err = NULL;

    if (!g_task_propagate_boolean(G_TASK(res), &err)) {
        if (ctx->dialog_alive) {
            gtk_widget_set_sensitive(d->connect_btn, TRUE);
            gtk_button_set_label(GTK_BUTTON(d->connect_btn), "Connect");
            GtkAlertDialog *alert = gtk_alert_dialog_new("%s", err->message);
            gtk_alert_dialog_show(alert, ctx->dialog);
            g_object_unref(alert);
        }
        g_error_free(err);
        sftp_ctx_unref(ctx);
        return;
    }

    notes_window_ssh_connect(ctx->win, d->host, d->user, d->port, d->key, d->remote);
    if (ctx->dialog_alive)
        gtk_window_destroy(GTK_WINDOW(ctx->dialog));
    /* Release the task's ref (dialog destroy releases the dialog's ref) */
    sftp_ctx_unref(ctx);
}

static void on_sftp_connect(GtkButton *btn, gpointer data) {
    SftpCtx *ctx = data;
    const char *host = gtk_editable_get_text(GTK_EDITABLE(ctx->host_entry));
    const char *user = gtk_editable_get_text(GTK_EDITABLE(ctx->user_entry));
    const char *remote = gtk_editable_get_text(GTK_EDITABLE(ctx->path_entry));
    const char *port_str = gtk_editable_get_text(GTK_EDITABLE(ctx->port_entry));
    gboolean use_key = gtk_check_button_get_active(ctx->use_key_check);

    if (!host[0] || !user[0]) return;
    if (!remote[0]) remote = "/";

    int port = atoi(port_str[0] ? port_str : "22");
    if (port <= 0) port = 22;

    gtk_widget_set_sensitive(GTK_WIDGET(btn), FALSE);
    gtk_button_set_label(btn, "Connecting...");

    GPtrArray *av = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(av, g_strdup("ssh"));
    g_ptr_array_add(av, g_strdup("-p"));
    g_ptr_array_add(av, g_strdup_printf("%d", port));
    g_ptr_array_add(av, g_strdup("-o"));
    g_ptr_array_add(av, g_strdup("StrictHostKeyChecking=accept-new"));
    g_ptr_array_add(av, g_strdup("-o"));
    g_ptr_array_add(av, g_strdup("BatchMode=yes"));
    g_ptr_array_add(av, g_strdup("-o"));
    g_ptr_array_add(av, g_strdup("ConnectTimeout=10"));
    if (use_key) {
        const char *key = gtk_editable_get_text(GTK_EDITABLE(ctx->key_entry));
        if (key[0]) {
            g_ptr_array_add(av, g_strdup("-i"));
            g_ptr_array_add(av, g_strdup(key));
        }
    }
    g_ptr_array_add(av, g_strdup_printf("%s@%s", user, host));
    g_ptr_array_add(av, g_strdup("--"));
    g_ptr_array_add(av, g_strdup("echo"));
    g_ptr_array_add(av, g_strdup("ok"));

    ConnectTaskData *td = g_new0(ConnectTaskData, 1);
    td->ctx = sftp_ctx_ref(ctx);  /* prevent use-after-free if dialog closes */
    td->argv = av;
    td->connect_btn = GTK_WIDGET(btn);
    g_strlcpy(td->host, host, sizeof(td->host));
    g_strlcpy(td->user, user, sizeof(td->user));
    td->port = port;
    if (use_key) {
        const char *key = gtk_editable_get_text(GTK_EDITABLE(ctx->key_entry));
        g_strlcpy(td->key, key, sizeof(td->key));
    }
    g_strlcpy(td->remote, remote, sizeof(td->remote));

    GTask *task = g_task_new(NULL, NULL, ssh_connect_done, td);
    g_task_set_task_data(task, td, connect_task_data_free);
    g_task_run_in_thread(task, ssh_connect_thread);
    g_object_unref(task);
}

static void on_sftp_dialog_destroy(GtkWidget *widget, gpointer data) {
    (void)widget;
    SftpCtx *ctx = data;
    ctx->dialog_alive = FALSE;
    sftp_ctx_unref(ctx);
}

static void on_sftp_dialog(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;

    SftpCtx *ctx = g_new0(SftpCtx, 1);
    ctx->win = win;
    ctx->selected_idx = -1;
    ctx->ref_count = 1;       /* owned by dialog destroy */
    ctx->dialog_alive = TRUE;
    connections_load(&ctx->conns);

    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "SFTP/SSH Connection");
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win->window));
    gtk_window_set_default_size(GTK_WINDOW(dialog), 520, 460);
    gtk_window_set_titlebar(GTK_WINDOW(dialog), adw_header_bar_new());
    ctx->dialog = GTK_WINDOW(dialog);

    g_signal_connect(dialog, "destroy", G_CALLBACK(on_sftp_dialog_destroy), ctx);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(hbox, 12);
    gtk_widget_set_margin_end(hbox, 12);
    gtk_widget_set_margin_top(hbox, 12);
    gtk_widget_set_margin_bottom(hbox, 12);
    gtk_window_set_child(GTK_WINDOW(dialog), hbox);

    /* Left: saved connections list */
    GtkWidget *left_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_size_request(left_box, 160, -1);

    GtkWidget *list_label = gtk_label_new("Connections");
    gtk_label_set_xalign(GTK_LABEL(list_label), 0);
    gtk_box_append(GTK_BOX(left_box), list_label);

    GtkWidget *list_scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(list_scroll, TRUE);
    ctx->conn_list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(ctx->conn_list, GTK_SELECTION_SINGLE);
    g_signal_connect(ctx->conn_list, "row-activated", G_CALLBACK(on_conn_selected), ctx);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(list_scroll), GTK_WIDGET(ctx->conn_list));
    gtk_box_append(GTK_BOX(left_box), list_scroll);

    gtk_box_append(GTK_BOX(hbox), left_box);
    gtk_box_append(GTK_BOX(hbox), gtk_separator_new(GTK_ORIENTATION_VERTICAL));

    /* Right: form */
    GtkWidget *right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(right_box, TRUE);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    int row = 0;

    gtk_grid_attach(GTK_GRID(grid), make_label("Name:"), 0, row, 1, 1);
    ctx->name_entry = GTK_ENTRY(gtk_entry_new());
    gtk_widget_set_hexpand(GTK_WIDGET(ctx->name_entry), TRUE);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(ctx->name_entry), 1, row++, 2, 1);

    gtk_grid_attach(GTK_GRID(grid), make_label("Host:"), 0, row, 1, 1);
    ctx->host_entry = GTK_ENTRY(gtk_entry_new());
    gtk_widget_set_hexpand(GTK_WIDGET(ctx->host_entry), TRUE);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(ctx->host_entry), 1, row++, 2, 1);

    gtk_grid_attach(GTK_GRID(grid), make_label("Port:"), 0, row, 1, 1);
    ctx->port_entry = GTK_ENTRY(gtk_entry_new());
    gtk_editable_set_text(GTK_EDITABLE(ctx->port_entry), "22");
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(ctx->port_entry), 1, row++, 2, 1);

    gtk_grid_attach(GTK_GRID(grid), make_label("User:"), 0, row, 1, 1);
    ctx->user_entry = GTK_ENTRY(gtk_entry_new());
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(ctx->user_entry), 1, row++, 2, 1);

    gtk_grid_attach(GTK_GRID(grid), make_label("Remote Path:"), 0, row, 1, 1);
    ctx->path_entry = GTK_ENTRY(gtk_entry_new());
    gtk_editable_set_text(GTK_EDITABLE(ctx->path_entry), "/");
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(ctx->path_entry), 1, row++, 2, 1);

    gtk_grid_attach(GTK_GRID(grid), make_label("Use Private Key:"), 0, row, 1, 1);
    ctx->use_key_check = GTK_CHECK_BUTTON(gtk_check_button_new());
    g_signal_connect(ctx->use_key_check, "toggled", G_CALLBACK(on_use_key_toggled), ctx);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(ctx->use_key_check), 1, row++, 2, 1);

    GtkWidget *key_lbl = make_label("Key File:");
    gtk_grid_attach(GTK_GRID(grid), key_lbl, 0, row, 1, 1);
    ctx->key_entry = GTK_ENTRY(gtk_entry_new());
    gtk_widget_set_hexpand(GTK_WIDGET(ctx->key_entry), TRUE);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(ctx->key_entry), 1, row, 1, 1);
    ctx->key_row = key_lbl;

    ctx->key_browse_btn = gtk_button_new_with_label("...");
    g_signal_connect(ctx->key_browse_btn, "clicked", G_CALLBACK(on_key_browse), ctx);
    gtk_grid_attach(GTK_GRID(grid), ctx->key_browse_btn, 2, row++, 1, 1);
    ctx->key_btn_row = ctx->key_browse_btn;

    sftp_update_auth_visibility(ctx);
    gtk_box_append(GTK_BOX(right_box), grid);

    /* Buttons */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_END);
    gtk_widget_set_margin_top(btn_box, 12);
    gtk_widget_set_valign(btn_box, GTK_ALIGN_END);
    gtk_widget_set_vexpand(btn_box, TRUE);

    GtkWidget *new_btn = gtk_button_new_with_label("New");
    g_signal_connect(new_btn, "clicked", G_CALLBACK(on_sftp_new), ctx);
    gtk_box_append(GTK_BOX(btn_box), new_btn);

    GtkWidget *del_btn = gtk_button_new_with_label("Delete");
    gtk_widget_add_css_class(del_btn, "destructive-action");
    g_signal_connect(del_btn, "clicked", G_CALLBACK(on_sftp_delete), ctx);
    gtk_box_append(GTK_BOX(btn_box), del_btn);

    GtkWidget *save_btn = gtk_button_new_with_label("Save");
    g_signal_connect(save_btn, "clicked", G_CALLBACK(on_sftp_save), ctx);
    gtk_box_append(GTK_BOX(btn_box), save_btn);

    GtkWidget *connect_btn = gtk_button_new_with_label("Connect");
    gtk_widget_add_css_class(connect_btn, "suggested-action");
    g_signal_connect(connect_btn, "clicked", G_CALLBACK(on_sftp_connect), ctx);
    gtk_box_append(GTK_BOX(btn_box), connect_btn);

    gtk_box_append(GTK_BOX(right_box), btn_box);
    gtk_box_append(GTK_BOX(hbox), right_box);

    sftp_populate_list(ctx);
    gtk_window_present(GTK_WINDOW(dialog));
}

/* ── Open Remote File browser dialog ── */

typedef struct {
    NotesWindow *win;
    GtkWindow   *dialog;
    GtkLabel    *path_label;
    GtkListBox  *file_list;
    char         current_dir[4096];
} OpenRemoteCtx;

static void remote_browse_populate(OpenRemoteCtx *ctx);

static void on_remote_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box;
    OpenRemoteCtx *ctx = data;
    if (!row) return;

    GtkWidget *lbl = gtk_widget_get_first_child(GTK_WIDGET(row));
    if (!lbl) return;
    const char *name = gtk_label_get_text(GTK_LABEL(lbl));
    if (!name || !name[0]) return;

    size_t nlen = strlen(name);

    /* ".." entry — go up */
    if (strcmp(name, "..") == 0) {
        /* Strip trailing slash */
        size_t dlen = strlen(ctx->current_dir);
        if (dlen > 1 && ctx->current_dir[dlen - 1] == '/')
            ctx->current_dir[dlen - 1] = '\0';
        char *last = strrchr(ctx->current_dir, '/');
        if (last && last != ctx->current_dir)
            *(last + 1) = '\0';
        else
            g_strlcpy(ctx->current_dir, "/", sizeof(ctx->current_dir));
        remote_browse_populate(ctx);
        return;
    }

    /* Directory — trailing slash */
    if (name[nlen - 1] == '/') {
        size_t dlen = strlen(ctx->current_dir);
        if (dlen > 0 && ctx->current_dir[dlen - 1] != '/')
            g_strlcat(ctx->current_dir, "/", sizeof(ctx->current_dir));
        g_strlcat(ctx->current_dir, name, sizeof(ctx->current_dir));
        remote_browse_populate(ctx);
        return;
    }

    /* File — open it */
    size_t dlen = strlen(ctx->current_dir);
    char *full_path = (dlen > 0 && ctx->current_dir[dlen - 1] == '/')
        ? g_strdup_printf("%s%s", ctx->current_dir, name)
        : g_strdup_printf("%s/%s", ctx->current_dir, name);

    notes_window_open_remote_file(ctx->win, full_path);
    g_free(full_path);
    gtk_window_destroy(GTK_WINDOW(ctx->dialog));
}

typedef struct {
    GPtrArray *argv;
    char      *stdout_buf;
    gboolean   ok;
} BrowseTaskData;

static void browse_task_data_free(gpointer p) {
    BrowseTaskData *d = p;
    if (d->argv) g_ptr_array_unref(d->argv);
    g_free(d->stdout_buf);
    g_free(d);
}

static void browse_thread(GTask *task, gpointer src, gpointer data,
                           GCancellable *cancel) {
    (void)src; (void)cancel;
    BrowseTaskData *d = data;
    d->ok = ssh_spawn_sync(d->argv, &d->stdout_buf, NULL);
    g_task_return_boolean(task, TRUE);
}

static void browse_done(GObject *src, GAsyncResult *res, gpointer data) {
    (void)src;
    OpenRemoteCtx *ctx = data;
    BrowseTaskData *d = g_task_get_task_data(G_TASK(res));

    /* Clear loading indicator */
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(ctx->file_list))))
        gtk_list_box_remove(ctx->file_list, child);

    /* Always show ".." unless at root */
    if (strcmp(ctx->current_dir, "/") != 0) {
        GtkWidget *lbl = gtk_label_new("..");
        gtk_label_set_xalign(GTK_LABEL(lbl), 0);
        gtk_widget_set_margin_start(lbl, 8);
        gtk_widget_set_margin_top(lbl, 2);
        gtk_widget_set_margin_bottom(lbl, 2);
        gtk_list_box_append(ctx->file_list, lbl);
    }

    if (!d->ok || !d->stdout_buf) {
        GtkWidget *lbl = gtk_label_new("(failed to list directory)");
        gtk_list_box_append(ctx->file_list, lbl);
        return;
    }

    /* Parse lines — directories first, then files */
    GPtrArray *dirs = g_ptr_array_new_with_free_func(g_free);
    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);

    char **lines = g_strsplit(d->stdout_buf, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        size_t len = strlen(lines[i]);
        if (len == 0) continue;
        if (lines[i][len - 1] == '/')
            g_ptr_array_add(dirs, g_strdup(lines[i]));
        else
            g_ptr_array_add(files, g_strdup(lines[i]));
    }
    g_strfreev(lines);

    /* Add directories */
    for (guint i = 0; i < dirs->len; i++) {
        const char *name = g_ptr_array_index(dirs, i);
        GtkWidget *lbl = gtk_label_new(name);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0);
        gtk_widget_set_margin_start(lbl, 8);
        gtk_widget_set_margin_top(lbl, 2);
        gtk_widget_set_margin_bottom(lbl, 2);
        gtk_list_box_append(ctx->file_list, lbl);
    }

    /* Add files */
    for (guint i = 0; i < files->len; i++) {
        const char *name = g_ptr_array_index(files, i);
        GtkWidget *lbl = gtk_label_new(name);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0);
        gtk_widget_set_margin_start(lbl, 8);
        gtk_widget_set_margin_top(lbl, 2);
        gtk_widget_set_margin_bottom(lbl, 2);
        gtk_list_box_append(ctx->file_list, lbl);
    }

    g_ptr_array_unref(dirs);
    g_ptr_array_unref(files);
}

static void remote_browse_populate(OpenRemoteCtx *ctx) {
    /* Update path label */
    char *label_text = g_strdup_printf("%s@%s:%s",
             ctx->win->ssh_user, ctx->win->ssh_host, ctx->current_dir);
    gtk_label_set_text(ctx->path_label, label_text);
    g_free(label_text);

    /* Clear list */
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(ctx->file_list))))
        gtk_list_box_remove(ctx->file_list, child);

    /* Show loading indicator */
    GtkWidget *loading = gtk_label_new("Loading...");
    gtk_list_box_append(ctx->file_list, loading);

    /* Run SSH ls asynchronously to avoid blocking UI */
    GPtrArray *av = ssh_argv_new(ctx->win->ssh_host, ctx->win->ssh_user,
                                  ctx->win->ssh_port, ctx->win->ssh_key,
                                  ctx->win->ssh_ctl_path);
    if (!av) {
        GtkWidget *lbl = gtk_label_new("(invalid SSH host/user)");
        gtk_list_box_append(ctx->file_list, lbl);
        return;
    }
    g_ptr_array_add(av, g_strdup("--"));
    g_ptr_array_add(av, g_strdup("ls"));
    g_ptr_array_add(av, g_strdup("-1pA"));
    g_ptr_array_add(av, g_strdup(ctx->current_dir));

    BrowseTaskData *d = g_new0(BrowseTaskData, 1);
    d->argv = av;

    GTask *task = g_task_new(NULL, NULL, browse_done, ctx);
    g_task_set_task_data(task, d, browse_task_data_free);
    g_task_run_in_thread(task, browse_thread);
    g_object_unref(task);
}

static void on_open_remote_destroy(GtkWidget *w, gpointer data) {
    (void)w;
    g_free(data);
}

static gboolean on_open_remote_key(GtkEventControllerKey *ctrl, guint keyval,
                                    guint keycode, GdkModifierType state, gpointer data) {
    (void)ctrl; (void)keycode; (void)state;
    OpenRemoteCtx *ctx = data;
    if (keyval == GDK_KEY_Escape) {
        gtk_window_destroy(GTK_WINDOW(ctx->dialog));
        return TRUE;
    }
    return FALSE;
}

static void on_open_remote(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;



    OpenRemoteCtx *ctx = g_new0(OpenRemoteCtx, 1);
    ctx->win = win;
    g_strlcpy(ctx->current_dir, win->ssh_remote_path, sizeof(ctx->current_dir));

    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Open Remote File");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 450, 500);
    gtk_window_set_titlebar(GTK_WINDOW(dialog), adw_header_bar_new());
    ctx->dialog = GTK_WINDOW(dialog);

    g_signal_connect(dialog, "destroy", G_CALLBACK(on_open_remote_destroy), ctx);

    GtkEventController *key = gtk_event_controller_key_new();
    g_signal_connect(key, "key-pressed", G_CALLBACK(on_open_remote_key), ctx);
    gtk_widget_add_controller(dialog, key);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(vbox, 8);
    gtk_widget_set_margin_end(vbox, 8);
    gtk_widget_set_margin_top(vbox, 8);
    gtk_widget_set_margin_bottom(vbox, 8);

    /* Path label */
    ctx->path_label = GTK_LABEL(gtk_label_new(""));
    gtk_label_set_xalign(ctx->path_label, 0);
    gtk_label_set_ellipsize(ctx->path_label, PANGO_ELLIPSIZE_START);
    gtk_box_append(GTK_BOX(vbox), GTK_WIDGET(ctx->path_label));

    /* File list in scrolled window */
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    ctx->file_list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(ctx->file_list, GTK_SELECTION_SINGLE);
    g_signal_connect(ctx->file_list, "row-activated", G_CALLBACK(on_remote_row_activated), ctx);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), GTK_WIDGET(ctx->file_list));
    gtk_box_append(GTK_BOX(vbox), scroll);

    gtk_window_set_child(GTK_WINDOW(dialog), vbox);

    /* Populate initial directory */
    remote_browse_populate(ctx);

    gtk_window_present(GTK_WINDOW(dialog));
}

static void on_sftp_disconnect(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    NotesWindow *win = data;
    notes_window_ssh_disconnect(win);
}

static void on_toggle_preview(GSimpleAction *action, GVariant *param, gpointer data) {
    (void)action; (void)param;
    preview_toggle((NotesWindow *)data);
}

void actions_setup(NotesWindow *win, GtkApplication *app) {
    static const GActionEntry win_entries[] = {
        {"new-file",    on_new_file,   NULL, NULL, NULL, {0}},
        {"save",        on_save,       NULL, NULL, NULL, {0}},
        {"save-as",     on_save_as,    NULL, NULL, NULL, {0}},
        {"open-file",   on_open_file,  NULL, NULL, NULL, {0}},
        {"settings",    on_settings,   NULL, NULL, NULL, {0}},
        {"find",         on_find,         NULL, NULL, NULL, {0}},
        {"find-replace", on_find_replace, NULL, NULL, NULL, {0}},
        {"goto-line",    on_goto_line,    NULL, NULL, NULL, {0}},
        {"sftp-connect",   on_sftp_dialog,    NULL, NULL, NULL, {0}},
        {"sftp-disconnect", on_sftp_disconnect, NULL, NULL, NULL, {0}},
        {"open-remote",  on_open_remote, NULL, NULL, NULL, {0}},
        {"zoom-in",     on_zoom_in,    NULL, NULL, NULL, {0}},
        {"zoom-out",    on_zoom_out,   NULL, NULL, NULL, {0}},
        {"toggle-preview", on_toggle_preview, NULL, NULL, NULL, {0}},
        {"export-pdf",  on_export_pdf,  NULL, NULL, NULL, {0}},
    };
    g_action_map_add_action_entries(G_ACTION_MAP(win->window),
                                   win_entries, G_N_ELEMENTS(win_entries), win);

    const char *zoom_in_accels[]  = {"<Control>plus", "<Control>equal", NULL};
    const char *zoom_out_accels[] = {"<Control>minus", NULL};
    const char *quit_accels[]     = {"<Control>q", NULL};
    const char *open_accels[]     = {"<Control>o", NULL};
    const char *save_accels[]     = {"<Control>s", NULL};
    const char *new_accels[]      = {"<Control>n", NULL};
    const char *save_as_accels[]  = {"<Control><Shift>s", NULL};
    const char *find_accels[]     = {"<Control>f", NULL};
    const char *replace_accels[]  = {"<Control>h", NULL};
    const char *goto_accels[]     = {"<Control>g", NULL};

    gtk_application_set_accels_for_action(app, "win.find",       find_accels);
    gtk_application_set_accels_for_action(app, "win.find-replace", replace_accels);
    gtk_application_set_accels_for_action(app, "win.goto-line",  goto_accels);
    gtk_application_set_accels_for_action(app, "win.zoom-in",   zoom_in_accels);
    gtk_application_set_accels_for_action(app, "win.zoom-out",  zoom_out_accels);
    gtk_application_set_accels_for_action(app, "app.quit",      quit_accels);
    gtk_application_set_accels_for_action(app, "win.open-file", open_accels);
    gtk_application_set_accels_for_action(app, "win.save",      save_accels);
    gtk_application_set_accels_for_action(app, "win.new-file",  new_accels);
    gtk_application_set_accels_for_action(app, "win.save-as",   save_as_accels);

    const char *preview_accels[] = {"<Control>p", NULL};
    gtk_application_set_accels_for_action(app, "win.toggle-preview", preview_accels);

    const char *pdf_accels[] = {"<Control><Shift>p", NULL};
    gtk_application_set_accels_for_action(app, "win.export-pdf", pdf_accels);
}
