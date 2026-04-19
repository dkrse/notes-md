#define _GNU_SOURCE
#include <adwaita.h>
#include "window.h"
#include "actions.h"
#include "preview.h"
#include "ssh.h"
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* FNV-1a 32-bit hash for fast dirty detection */
guint32 fnv1a_hash(const char *data, gsize len) {
    guint32 h = 2166136261u;
    for (gsize i = 0; i < len; i++) {
        h ^= (guint8)data[i];
        h *= 16777619u;
    }
    return h;
}

/* Custom CSS themes */
typedef struct {
    const char *name;
    const char *fg;
    const char *bg;
    const char *css;
} ThemeDef;

static const ThemeDef custom_themes[] = {
    {"solarized-light", "#657b83", "#fdf6e3",
     "textview text { background-color: #fdf6e3; color: #657b83; }"
     "textview { background-color: #fdf6e3; }"},
    {"solarized-dark", "#839496", "#002b36",
     "textview text { background-color: #002b36; color: #839496; }"
     "textview { background-color: #002b36; }"},
    {"monokai", "#f8f8f2", "#272822",
     "textview text { background-color: #272822; color: #f8f8f2; }"
     "textview { background-color: #272822; }"},
    {"gruvbox-light", "#3c3836", "#fbf1c7",
     "textview text { background-color: #fbf1c7; color: #3c3836; }"
     "textview { background-color: #fbf1c7; }"},
    {"gruvbox-dark", "#ebdbb2", "#282828",
     "textview text { background-color: #282828; color: #ebdbb2; }"
     "textview { background-color: #282828; }"},
    {"nord", "#d8dee9", "#2e3440",
     "textview text { background-color: #2e3440; color: #d8dee9; }"
     "textview { background-color: #2e3440; }"},
    {"dracula", "#f8f8f2", "#282a36",
     "textview text { background-color: #282a36; color: #f8f8f2; }"
     "textview { background-color: #282a36; }"},
    {"tokyo-night", "#a9b1d6", "#1a1b26",
     "textview text { background-color: #1a1b26; color: #a9b1d6; }"
     "textview { background-color: #1a1b26; }"},
    {"catppuccin-latte", "#4c4f69", "#eff1f5",
     "textview text { background-color: #eff1f5; color: #4c4f69; }"
     "textview { background-color: #eff1f5; }"},
    {"catppuccin-mocha", "#cdd6f4", "#1e1e2e",
     "textview text { background-color: #1e1e2e; color: #cdd6f4; }"
     "textview { background-color: #1e1e2e; }"},
    {NULL, NULL, NULL, NULL}
};

static gboolean is_dark_theme(const char *theme);

/*
 * Highlight current line: overlay approach (like VS Code, Sublime).
 */

#define NOTES_TYPE_TEXT_VIEW (notes_text_view_get_type())
G_DECLARE_FINAL_TYPE(NotesTextView, notes_text_view, NOTES, TEXT_VIEW, GtkSourceView)

struct _NotesTextView {
    GtkSourceView parent;
    NotesWindow *win;
};

G_DEFINE_TYPE(NotesTextView, notes_text_view, GTK_SOURCE_TYPE_VIEW)

static void notes_text_view_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
    NotesTextView *self = NOTES_TEXT_VIEW(widget);
    NotesWindow *win = self->win;

    GTK_WIDGET_CLASS(notes_text_view_parent_class)->snapshot(widget, snapshot);

    if (win && win->settings.highlight_current_line) {
        GtkTextIter iter;
        gtk_text_buffer_get_iter_at_line(win->buffer, &iter, win->highlight_line);

        int buf_y, line_height;
        gtk_text_view_get_line_yrange(GTK_TEXT_VIEW(widget), &iter,
                                      &buf_y, &line_height);

        int wx, wy;
        gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(widget),
            GTK_TEXT_WINDOW_WIDGET, 0, buf_y, &wx, &wy);

        int view_width = gtk_widget_get_width(widget);
        int h = line_height > 0 ? line_height : win->settings.font_size + 4;
        int extra = (int)((win->settings.line_spacing - 1.0) * win->settings.font_size * 0.5);
        if (extra < 0) extra = 0;

        graphene_rect_t area = GRAPHENE_RECT_INIT(0, wy - extra, view_width, h + extra * 2);
        gtk_snapshot_append_color(snapshot, &win->highlight_rgba, &area);
    }
}

static void notes_text_view_class_init(NotesTextViewClass *klass) {
    GTK_WIDGET_CLASS(klass)->snapshot = notes_text_view_snapshot;
}

static void notes_text_view_init(NotesTextView *self) {
    (void)self;
}

static void update_line_highlights(NotesWindow *win) {
    GtkTextMark *mark = gtk_text_buffer_get_insert(win->buffer);
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(win->buffer, &cursor, mark);
    win->highlight_line = gtk_text_iter_get_line(&cursor);
    gtk_widget_queue_draw(GTK_WIDGET(win->text_view));
}

static void apply_highlight_color(NotesWindow *win) {
    gboolean dark = is_dark_theme(win->settings.theme);
    if (dark) {
        win->highlight_rgba = (GdkRGBA){1.0, 1.0, 1.0, 0.06};
    } else {
        win->highlight_rgba = (GdkRGBA){0.0, 0.0, 0.0, 0.06};
    }
}

static const char *scheme_for_theme(const char *theme) {
    if (strcmp(theme, "solarized-light") == 0) return "solarized-light";
    if (strcmp(theme, "solarized-dark") == 0)  return "solarized-dark";
    if (strcmp(theme, "monokai") == 0)         return "oblivion";
    if (strcmp(theme, "gruvbox-dark") == 0)     return "classic-dark";
    if (strcmp(theme, "gruvbox-light") == 0)    return "kate";
    if (strcmp(theme, "nord") == 0)             return "cobalt";
    if (strcmp(theme, "dracula") == 0)          return "oblivion";
    if (strcmp(theme, "tokyo-night") == 0)      return "Adwaita-dark";
    if (strcmp(theme, "catppuccin-mocha") == 0) return "Adwaita-dark";
    if (strcmp(theme, "catppuccin-latte") == 0) return "Adwaita";
    if (strcmp(theme, "dark") == 0)             return "Adwaita-dark";
    if (strcmp(theme, "light") == 0)            return "Adwaita";
    /* system */
    return "Adwaita";
}

static void apply_source_style(NotesWindow *win) {
    GtkSourceStyleSchemeManager *sm = gtk_source_style_scheme_manager_get_default();
    const char *scheme_id = scheme_for_theme(win->settings.theme);

    /* For "system" theme, pick based on current dark mode */
    if (strcmp(win->settings.theme, "system") == 0) {
        AdwStyleManager *adw = adw_style_manager_get_default();
        if (adw_style_manager_get_dark(adw))
            scheme_id = "Adwaita-dark";
        else
            scheme_id = "Adwaita";
    }

    GtkSourceStyleScheme *scheme = gtk_source_style_scheme_manager_get_scheme(sm, scheme_id);
    if (scheme)
        gtk_source_buffer_set_style_scheme(win->source_buffer, scheme);

    gtk_source_buffer_set_highlight_syntax(win->source_buffer, win->settings.highlight_syntax);
}

static void apply_source_language(NotesWindow *win, const char *path) {
    if (!win->settings.highlight_syntax) {
        gtk_source_buffer_set_language(win->source_buffer, NULL);
        return;
    }
    GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default();

    /* Add custom language specs path (next to executable or installed) */
    static gboolean paths_set = FALSE;
    if (!paths_set) {
        const gchar * const *old = gtk_source_language_manager_get_search_path(lm);
        GPtrArray *dirs = g_ptr_array_new();

        /* App-local data dir */
        char exe_dir[1024];
        ssize_t n = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
        if (n > 0) {
            exe_dir[n] = '\0';
            char *slash = strrchr(exe_dir, '/');
            if (slash) *slash = '\0';
            char custom[1088];
            snprintf(custom, sizeof(custom), "%s/../data/language-specs", exe_dir);
            g_ptr_array_add(dirs, g_strdup(custom));
            /* Also check installed location */
            snprintf(custom, sizeof(custom), "%s/data/language-specs", exe_dir);
            g_ptr_array_add(dirs, g_strdup(custom));
        }

        for (int i = 0; old && old[i]; i++)
            g_ptr_array_add(dirs, g_strdup(old[i]));
        g_ptr_array_add(dirs, NULL);
        gtk_source_language_manager_set_search_path(lm, (const gchar * const *)dirs->pdata);
        g_ptr_array_unref(dirs);
        paths_set = TRUE;
    }

    GtkSourceLanguage *lang = gtk_source_language_manager_guess_language(lm, path, NULL);

    /* Fallback: try to detect Makefile by basename */
    if (!lang) {
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        if (g_ascii_strncasecmp(base, "Makefile", 8) == 0 ||
            g_ascii_strncasecmp(base, "GNUmakefile", 11) == 0)
            lang = gtk_source_language_manager_get_language(lm, "makefile");
    }

    gtk_source_buffer_set_language(win->source_buffer, lang);
}

/* Escape a string for safe CSS embedding: strip } ; { and quotes */
static void css_escape_font(char *out, size_t out_sz, const char *in) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j < out_sz - 1; i++) {
        char c = in[i];
        if (c == '}' || c == '{' || c == ';' || c == '"' || c == '\'' || c == '\\')
            continue;
        out[j++] = c;
    }
    out[j] = '\0';
}

static void apply_css(NotesWindow *win) {
    char css[4096];

    /* Escape font names to prevent CSS injection */
    char safe_font[256], safe_gui_font[256];
    css_escape_font(safe_font, sizeof(safe_font), win->settings.font);
    css_escape_font(safe_gui_font, sizeof(safe_gui_font), win->settings.gui_font);

    const ThemeDef *td = NULL;
    for (int i = 0; custom_themes[i].name; i++) {
        if (strcmp(win->settings.theme, custom_themes[i].name) == 0) {
            td = &custom_themes[i];
            break;
        }
    }

    if (td) {
        const char *bg = td->bg;
        const char *fg = td->fg;
        gboolean hl = win->settings.highlight_syntax;

        snprintf(css, sizeof(css),
            "textview { font-family: %s; font-size: %dpt; background-color: %s; }"
            "textview text { background-color: %s;%s%s%s }"
            ".line-numbers, .line-numbers text {"
            "  background-color: %s; color: alpha(%s, 0.3); }"
            ".titlebar, headerbar {"
            "  background: %s; color: %s; box-shadow: none; }"
            "headerbar button, headerbar menubutton button,"
            "headerbar menubutton { color: %s; background: transparent; }"
            "headerbar button:hover, headerbar menubutton button:hover {"
            "  background: alpha(%s, 0.1); }"
            ".statusbar { font-size: 10pt; padding: 2px 4px;"
            "  color: alpha(%s, 0.6); background-color: %s; }"
            "window, window.background { background-color: %s; color: %s; }"
            "popover, popover.menu {"
            "  background: transparent; box-shadow: none; border: none; }"
            "popover > contents, popover.menu > contents {"
            "  background-color: %s; color: %s;"
            "  border-radius: 12px; border: none; box-shadow: 0 2px 8px rgba(0,0,0,0.3); }"
            "popover > arrow, popover.menu > arrow { background: transparent; border: none; }"
            "popover modelbutton { color: %s; }"
            "popover modelbutton:hover { background-color: alpha(%s, 0.15); }"
            "windowcontrols button { color: %s; }"
            /* Dialog widgets */
            "label { color: %s; }"
            "entry { background-color: alpha(%s, 0.08); color: %s;"
            "  border: 1px solid alpha(%s, 0.2); }"
            "button { color: %s; }"
            "checkbutton { color: %s; }"
            "scale { color: %s; }"
            "list, listview, row { background-color: %s; color: %s; }"
            "row:hover { background-color: alpha(%s, 0.08); }"
            "row:selected { background-color: alpha(%s, 0.15); }"
            "scrolledwindow { background-color: %s; }"
            "separator { background-color: alpha(%s, 0.15); }",
            safe_font, win->settings.font_size, bg,
            bg, hl ? "" : " color: ", hl ? "" : fg, hl ? "" : ";",
            bg, fg,
            bg, fg,
            fg,
            fg,
            fg, bg,
            bg, fg,
            bg, fg,
            fg,
            fg,
            fg,
            /* Dialog widgets */
            fg,
            fg, fg,
            fg,
            fg,
            fg,
            fg,
            bg, fg,
            fg,
            fg,
            bg,
            fg);
    } else {
        const char *bg, *fg;
        if (is_dark_theme(win->settings.theme)) {
            bg = "#1e1e1e"; fg = "#d4d4d4";
        } else {
            bg = "#ffffff"; fg = "#1e1e1e";
        }
        gboolean hl = win->settings.highlight_syntax;

        snprintf(css, sizeof(css),
            "textview { font-family: %s; font-size: %dpt; background-color: %s; }"
            "textview text { background-color: %s;%s%s%s }"
            ".line-numbers, .line-numbers text {"
            "  background-color: %s; color: alpha(%s, 0.3); }"
            ".statusbar { font-size: 10pt; padding: 2px 4px; opacity: 0.7; }",
            safe_font, win->settings.font_size, bg,
            bg, hl ? "" : " color: ", hl ? "" : fg, hl ? "" : ";",
            bg, fg);
    }

    /* Append GUI font rule */
    char gui_css[512];
    snprintf(gui_css, sizeof(gui_css),
        "headerbar, headerbar button, headerbar label,"
        "popover, popover.menu, popover label, popover button,"
        ".statusbar, .statusbar label {"
        "  font-family: %s; font-size: %dpt; }",
        safe_gui_font, win->settings.gui_font_size);
    strncat(css, gui_css, sizeof(css) - strlen(css) - 1);

    gtk_css_provider_load_from_string(win->css_provider, css);
}

static gboolean is_dark_theme(const char *theme) {
    return strcmp(theme, "dark") == 0 ||
           strcmp(theme, "solarized-dark") == 0 ||
           strcmp(theme, "monokai") == 0 ||
           strcmp(theme, "gruvbox-dark") == 0 ||
           strcmp(theme, "nord") == 0 ||
           strcmp(theme, "dracula") == 0 ||
           strcmp(theme, "tokyo-night") == 0 ||
           strcmp(theme, "catppuccin-mocha") == 0;
}

static void apply_theme(NotesWindow *win) {
    AdwStyleManager *sm = adw_style_manager_get_default();
    gboolean dark = is_dark_theme(win->settings.theme);

    for (int i = 0; custom_themes[i].name; i++) {
        if (strcmp(win->settings.theme, custom_themes[i].name) == 0) {
            GdkRGBA c;
            gdk_rgba_parse(&c, custom_themes[i].bg);
            dark = (0.299 * c.red + 0.587 * c.green + 0.114 * c.blue) < 0.5;
            break;
        }
    }

    if (strcmp(win->settings.theme, "system") == 0)
        adw_style_manager_set_color_scheme(sm, ADW_COLOR_SCHEME_DEFAULT);
    else if (dark)
        adw_style_manager_set_color_scheme(sm, ADW_COLOR_SCHEME_FORCE_DARK);
    else
        adw_style_manager_set_color_scheme(sm, ADW_COLOR_SCHEME_FORCE_LIGHT);
}

/* Line numbers */
static void update_line_numbers(GtkTextBuffer *buffer, NotesWindow *win);

static void update_cursor_position(NotesWindow *win) {
    GtkTextIter iter;
    GtkTextMark *mark = gtk_text_buffer_get_insert(win->buffer);
    gtk_text_buffer_get_iter_at_mark(win->buffer, &iter, mark);
    int line = gtk_text_iter_get_line(&iter) + 1;
    int col = gtk_text_iter_get_line_offset(&iter) + 1;
    char buf[64];
    snprintf(buf, sizeof(buf), "Ln %d, Col %d", line, col);
    gtk_label_set_text(win->status_cursor, buf);
}

static void apply_font_intensity(NotesWindow *win);

static gboolean scroll_idle_cb(gpointer data) {
    NotesWindow *win = data;
    win->scroll_idle_id = 0;
    GtkTextMark *insert = gtk_text_buffer_get_insert(win->buffer);
    gtk_text_view_scroll_to_mark(win->text_view, insert, 0.05, FALSE, 0, 0);
    return G_SOURCE_REMOVE;
}

static gboolean intensity_idle_cb(gpointer data) {
    NotesWindow *win = data;
    win->intensity_idle_id = 0;
    if (win->settings.font_intensity < 0.99)
        apply_font_intensity(win);
    return G_SOURCE_REMOVE;
}

static void update_dirty_state(NotesWindow *win) {
    /* Check if content matches original via hash — O(n) but avoids full strcmp */
    if (win->original_content) {
        GtkTextIter s, e;
        gtk_text_buffer_get_bounds(win->buffer, &s, &e);
        char *text = gtk_text_buffer_get_text(win->buffer, &s, &e, FALSE);
        gsize len = strlen(text);
        guint32 h = fnv1a_hash(text, len);
        /* Hash match: verify with strcmp to avoid false positives */
        gboolean same = (h == win->original_hash) && (strcmp(text, win->original_content) == 0);
        g_free(text);

        if (same && win->dirty) {
            win->dirty = FALSE;
            if (win->current_file[0]) {
                char *base = g_path_get_basename(win->current_file);
                if (notes_window_is_remote(win)) {
                    char *title = g_strdup_printf("%s [%s@%s]", base, win->ssh_user, win->ssh_host);
                    gtk_window_set_title(GTK_WINDOW(win->window), title);
                    g_free(title);
                } else {
                    gtk_window_set_title(GTK_WINDOW(win->window), base);
                }
                g_free(base);
            } else {
                gtk_window_set_title(GTK_WINDOW(win->window), "Notes MD");
            }
            return;
        }
    }

    if (!win->dirty) {
        win->dirty = TRUE;
        if (win->current_file[0]) {
            char *base = g_path_get_basename(win->current_file);
            char title[256];
            snprintf(title, sizeof(title), "%s *", base);
            gtk_window_set_title(GTK_WINDOW(win->window), title);
            g_free(base);
        } else {
            gtk_window_set_title(GTK_WINDOW(win->window), "Notes MD *");
        }
    }
}

static void on_buffer_changed(GtkTextBuffer *buffer, gpointer data) {
    NotesWindow *win = data;
    update_dirty_state(win);
    if (win->settings.show_line_numbers)
        update_line_numbers(buffer, win);
    update_cursor_position(win);
    update_line_highlights(win);
    if (win->settings.font_intensity < 0.99 && win->intensity_idle_id == 0)
        win->intensity_idle_id = g_idle_add(intensity_idle_cb, win);
    if (win->scroll_idle_id == 0)
        win->scroll_idle_id = g_idle_add(scroll_idle_cb, win);
}

static void on_cursor_moved(GtkTextBuffer *buffer, GParamSpec *pspec, gpointer data) {
    (void)buffer; (void)pspec;
    NotesWindow *win = data;
    update_cursor_position(win);
    update_line_highlights(win);
}

static void draw_line_numbers(GtkDrawingArea *area, cairo_t *cr,
                              int width, int height, gpointer data) {
    (void)area; (void)height;
    NotesWindow *win = data;
    if (!win->settings.show_line_numbers) return;

    PangoFontDescription *fd = pango_font_description_new();
    pango_font_description_set_family(fd, win->settings.font);
    pango_font_description_set_size(fd, win->settings.font_size * PANGO_SCALE);

    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, fd);
    pango_layout_set_alignment(layout, PANGO_ALIGN_RIGHT);
    pango_layout_set_width(layout, (width - 12) * PANGO_SCALE);

    GdkRGBA color;
    const char *theme_fg = NULL;
    for (int t = 0; custom_themes[t].name; t++) {
        if (strcmp(win->settings.theme, custom_themes[t].name) == 0) {
            theme_fg = custom_themes[t].fg;
            break;
        }
    }
    if (theme_fg) {
        gdk_rgba_parse(&color, theme_fg);
    } else if (is_dark_theme(win->settings.theme)) {
        color = (GdkRGBA){0.85, 0.85, 0.85, 1.0};
    } else {
        color = (GdkRGBA){0.12, 0.12, 0.12, 1.0};
    }
    cairo_set_source_rgba(cr, color.red, color.green, color.blue, 0.3);

    GdkRectangle visible;
    gtk_text_view_get_visible_rect(win->text_view, &visible);

    GtkTextIter iter;
    gtk_text_view_get_iter_at_location(win->text_view, &iter,
                                       visible.x, visible.y);
    gtk_text_iter_set_line_offset(&iter, 0);

    while (TRUE) {
        int buf_y, line_height;
        gtk_text_view_get_line_yrange(win->text_view, &iter, &buf_y, &line_height);

        if (buf_y > visible.y + visible.height) break;

        int win_x, win_y;
        gtk_text_view_buffer_to_window_coords(win->text_view,
            GTK_TEXT_WINDOW_WIDGET, 0, buf_y, &win_x, &win_y);

        char num[16];
        snprintf(num, sizeof(num), "%d", gtk_text_iter_get_line(&iter) + 1);
        pango_layout_set_text(layout, num, -1);
        cairo_move_to(cr, 4, win_y);
        pango_cairo_show_layout(cr, layout);

        if (!gtk_text_iter_forward_line(&iter)) break;
    }

    g_object_unref(layout);
    pango_font_description_free(fd);
}

static gboolean line_numbers_idle_cb(gpointer data) {
    NotesWindow *win = data;
    win->line_numbers_idle_id = 0;
    if (!win->settings.show_line_numbers) return G_SOURCE_REMOVE;

    int lines = gtk_text_buffer_get_line_count(win->buffer);

    int digits = 1, n = lines;
    while (n >= 10) { digits++; n /= 10; }
    if (digits < 2) digits = 2;

    char sample[16];
    memset(sample, '9', (size_t)digits);
    sample[digits] = '\0';

    PangoLayout *layout = gtk_widget_create_pango_layout(GTK_WIDGET(win->line_numbers), sample);
    PangoFontDescription *fd = pango_font_description_new();
    pango_font_description_set_family(fd, win->settings.font);
    pango_font_description_set_size(fd, win->settings.font_size * PANGO_SCALE);
    pango_layout_set_font_description(layout, fd);
    int pw, ph;
    pango_layout_get_pixel_size(layout, &pw, &ph);
    (void)ph;
    pango_font_description_free(fd);
    g_object_unref(layout);

    int width = pw + 12;
    gtk_widget_set_size_request(GTK_WIDGET(win->line_numbers), width, -1);

    gtk_widget_queue_draw(GTK_WIDGET(win->line_numbers));
    return G_SOURCE_REMOVE;
}

static void update_line_numbers(GtkTextBuffer *buffer, NotesWindow *win) {
    (void)buffer;
    if (!win->settings.show_line_numbers) return;
    if (win->line_numbers_idle_id == 0)
        win->line_numbers_idle_id = g_idle_add(line_numbers_idle_cb, win);
}

static void apply_font_intensity(NotesWindow *win) {
    double alpha = win->settings.font_intensity;

    /* Remove old intensity tag */
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(win->buffer, &start, &end);
    gtk_text_buffer_remove_tag(win->buffer, win->intensity_tag, &start, &end);

    /* When syntax highlighting is on, use CSS opacity to preserve colors */
    if (win->settings.highlight_syntax) {
        gtk_widget_set_opacity(GTK_WIDGET(win->text_view),
                               alpha >= 0.99 ? 1.0 : alpha);
        return;
    }

    gtk_widget_set_opacity(GTK_WIDGET(win->text_view), 1.0);

    if (alpha >= 0.99)
        return;

    const char *fg = NULL;
    for (int i = 0; custom_themes[i].name; i++) {
        if (strcmp(win->settings.theme, custom_themes[i].name) == 0) {
            fg = custom_themes[i].fg;
            break;
        }
    }

    GdkRGBA color;
    if (fg) {
        gdk_rgba_parse(&color, fg);
    } else if (is_dark_theme(win->settings.theme)) {
        color = (GdkRGBA){1.0, 1.0, 1.0, 1.0};
    } else {
        color = (GdkRGBA){0.0, 0.0, 0.0, 1.0};
    }
    color.alpha = alpha;
    g_object_set(win->intensity_tag, "foreground-rgba", &color, NULL);

    gtk_text_buffer_get_bounds(win->buffer, &start, &end);
    gtk_text_buffer_apply_tag(win->buffer, win->intensity_tag, &start, &end);
}

void notes_window_apply_settings(NotesWindow *win) {
    apply_theme(win);
    apply_css(win);

    int extra = (int)((win->settings.line_spacing - 1.0) * win->settings.font_size * 0.5);
    if (extra < 0) extra = 0;
    gtk_text_view_set_pixels_above_lines(win->text_view, extra);
    gtk_text_view_set_pixels_below_lines(win->text_view, extra);

    gtk_widget_set_visible(win->ln_scrolled, win->settings.show_line_numbers);
    win->cached_line_count = 0;
    if (win->settings.show_line_numbers)
        update_line_numbers(win->buffer, win);

    gtk_text_view_set_wrap_mode(win->text_view,
        win->settings.wrap_lines ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);

    apply_highlight_color(win);
    update_line_highlights(win);
    apply_font_intensity(win);
    apply_source_style(win);
    preview_apply_layout(win);
    preview_apply_font_size(win);
}

/* Max bytes to load into GtkTextBuffer — keeps UI responsive */
#define MAX_DISPLAY_BYTES (5 * 1024 * 1024)  /* 5 MB */

static const char *human_size(gsize bytes) {
    static char buf[64];
    if (bytes >= 1024ULL * 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1f GB", (double)bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1f MB", (double)bytes / (1024.0 * 1024));
    else if (bytes >= 1024)
        snprintf(buf, sizeof(buf), "%.1f KB", (double)bytes / 1024.0);
    else
        snprintf(buf, sizeof(buf), "%lu B", (unsigned long)bytes);
    return buf;
}

void notes_window_load_file(NotesWindow *win, const char *path) {
    if (!path || path[0] == '\0') return;

    /* Get file size first */
    struct stat st;
    if (g_stat(path, &st) != 0) return;
    gsize file_size = (gsize)st.st_size;

    /* Determine how much to read */
    gboolean truncated = FALSE;
    gsize read_size = file_size;
    if (read_size > MAX_DISPLAY_BYTES) {
        read_size = MAX_DISPLAY_BYTES;
        truncated = TRUE;
    }

    /* Read only what we need */
    FILE *fp = fopen(path, "rb");
    if (!fp) return;
    char *contents = g_malloc(read_size + 1);
    gsize len = fread(contents, 1, read_size, fp);
    if (ferror(fp)) {
        fclose(fp);
        g_free(contents);
        return;
    }
    fclose(fp);
    contents[len] = '\0';

    /* Detect binary: check first 8KB for NUL bytes */
    gboolean is_binary = FALSE;
    gsize check_len = len < 8192 ? len : 8192;
    for (gsize i = 0; i < check_len; i++) {
        if (contents[i] == '\0') { is_binary = TRUE; break; }
    }

    /* Replace all NUL bytes with '.' */
    if (is_binary) {
        for (gsize i = 0; i < len; i++) {
            if (contents[i] == '\0') contents[i] = '.';
        }
    }

    /* Ensure valid UTF-8 — if not, convert lossily */
    if (!g_utf8_validate(contents, (gssize)len, NULL)) {
        is_binary = TRUE;
        gsize bytes_written = 0;
        char *utf8 = g_convert_with_fallback(contents, (gssize)len,
                         "UTF-8", "ISO-8859-1", ".", NULL, &bytes_written, NULL);
        if (utf8) {
            g_free(contents);
            contents = utf8;
            len = bytes_written;
        }
    }

    /* For binary/truncated files, skip original_content to save RAM */
    g_free(win->original_content);
    if (is_binary || truncated) {
        win->original_content = NULL;
        win->original_hash = 0;
    } else {
        win->original_hash = fnv1a_hash(contents, len);
        win->original_content = g_strndup(contents, len);
    }

    win->is_binary = is_binary;
    win->is_truncated = truncated;

    /* Set language BEFORE loading text so highlighting applies immediately */
    apply_source_language(win, path);
    apply_source_style(win);

    /* Block changed signal during load to prevent dirty state confusion */
    g_signal_handlers_block_by_func(win->buffer, on_buffer_changed, win);
    g_signal_handlers_block_by_func(win->buffer, on_cursor_moved, win);

    gtk_text_buffer_set_text(win->buffer, contents, (int)len);
    g_free(contents);

    GtkTextIter start;
    gtk_text_buffer_get_start_iter(win->buffer, &start);
    gtk_text_buffer_place_cursor(win->buffer, &start);

    /* Set state BEFORE unblock so any triggered callback sees correct values */
    win->dirty = FALSE;
    snprintf(win->current_file, sizeof(win->current_file), "%s", path);
    snprintf(win->settings.last_file, sizeof(win->settings.last_file), "%s", path);

    /* Show filename in title */
    char *base = g_path_get_basename(path);
    gtk_window_set_title(GTK_WINDOW(win->window), base);
    g_free(base);

    g_signal_handlers_unblock_by_func(win->buffer, on_buffer_changed, win);
    g_signal_handlers_unblock_by_func(win->buffer, on_cursor_moved, win);

    /* Status bar: show file info */
    char status[128];
    if (truncated)
        snprintf(status, sizeof(status), "UTF-8 | %s | showing first 5 MB of %s",
                 is_binary ? "BIN" : "TEXT", human_size(file_size));
    else
        snprintf(status, sizeof(status), "UTF-8 | %s | %s",
                 is_binary ? "BIN" : "TEXT", human_size(file_size));
    gtk_label_set_text(win->status_encoding, status);

    /* Update line numbers and cursor for loaded content */
    update_cursor_position(win);
    if (win->settings.show_line_numbers)
        update_line_numbers(win->buffer, win);
    update_line_highlights(win);
    apply_font_intensity(win);

    settings_save(&win->settings);
}

static void auto_save_current(NotesWindow *win) {
    if (!win->dirty) return;
    /* Never auto-save over binary/truncated files */
    if (win->is_binary || !win->original_content) return;

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(win->buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(win->buffer, &start, &end, FALSE);
    if (!text || text[0] == '\0') {
        g_free(text);
        return;
    }

    if (win->current_file[0] != '\0') {
        /* Atomic write: write to exclusive tmp, then rename */
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
        snprintf(win->settings.last_file, sizeof(win->settings.last_file), "%s", win->current_file);
    }
    g_free(win->original_content);
    win->original_content = text;
    win->original_hash = fnv1a_hash(text, strlen(text));
    win->dirty = FALSE;
}

static void close_and_cleanup(NotesWindow *win) {
    if (notes_window_is_remote(win))
        notes_window_ssh_disconnect(win);

    if (win->current_file[0] != '\0' && !ssh_path_is_remote(win->current_file))
        snprintf(win->settings.last_file, sizeof(win->settings.last_file),
                 "%s", win->current_file);

    win->settings.window_width = gtk_widget_get_width(GTK_WIDGET(win->window));
    win->settings.window_height = gtk_widget_get_height(GTK_WIDGET(win->window));
    settings_save(&win->settings);
    gtk_window_destroy(GTK_WINDOW(win->window));
}

static void on_close_save_response(GObject *src, GAsyncResult *res, gpointer data) {
    NotesWindow *win = data;
    GtkAlertDialog *dlg = GTK_ALERT_DIALOG(src);
    int btn = gtk_alert_dialog_choose_finish(dlg, res, NULL);

    if (btn == 0) {
        /* Save */
        auto_save_current(win);
        close_and_cleanup(win);
    } else if (btn == 1) {
        /* Don't Save */
        win->dirty = FALSE;
        close_and_cleanup(win);
    }
    /* btn == 2 or -1 (Cancel / closed) — do nothing, stay open */
}

static gboolean on_close_request(GtkWindow *window, gpointer data) {
    (void)window;
    NotesWindow *win = data;

    if (!win->dirty) {
        close_and_cleanup(win);
        return TRUE;
    }

    /* Show save confirmation dialog */
    GtkAlertDialog *dlg = gtk_alert_dialog_new("Save changes before closing?");
    gtk_alert_dialog_set_detail(dlg, "If you don't save, your changes will be lost.");
    const char *buttons[] = {"Save", "Don't Save", "Cancel", NULL};
    gtk_alert_dialog_set_buttons(dlg, buttons);
    gtk_alert_dialog_set_default_button(dlg, 0);
    gtk_alert_dialog_set_cancel_button(dlg, 2);
    gtk_alert_dialog_choose(dlg, GTK_WINDOW(win->window), NULL, on_close_save_response, win);
    g_object_unref(dlg);

    return TRUE; /* block close until user responds */
}

static void on_destroy(GtkWidget *widget, gpointer data) {
    (void)widget;
    NotesWindow *win = data;

    if (win->intensity_idle_id) {
        g_source_remove(win->intensity_idle_id);
        win->intensity_idle_id = 0;
    }
    if (win->scroll_idle_id) {
        g_source_remove(win->scroll_idle_id);
        win->scroll_idle_id = 0;
    }
    if (win->line_numbers_idle_id) {
        g_source_remove(win->line_numbers_idle_id);
        win->line_numbers_idle_id = 0;
    }
    if (win->title_idle_id) {
        g_source_remove(win->title_idle_id);
        win->title_idle_id = 0;
    }
    gtk_style_context_remove_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(win->css_provider));
    g_object_unref(win->css_provider);

    g_free(win->match_lines);
    g_free(win->match_offsets);
    g_free(win->original_content);
    g_free(win);
}

static GtkWidget *build_menu_button(void) {
    GMenu *menu = g_menu_new();
    g_menu_append(menu, "New", "win.new-file");
    g_menu_append(menu, "Open File", "win.open-file");
    g_menu_append(menu, "Save", "win.save");
    g_menu_append(menu, "Save As...", "win.save-as");
    g_menu_append(menu, "Export to PDF...", "win.export-pdf");
    g_menu_append(menu, "Find", "win.find");
    g_menu_append(menu, "Find & Replace", "win.find-replace");
    g_menu_append(menu, "Go to Line", "win.goto-line");
    g_menu_append(menu, "SFTP Connect...", "win.sftp-connect");
    g_menu_append(menu, "Open Remote File", "win.open-remote");
    g_menu_append(menu, "SFTP Disconnect", "win.sftp-disconnect");
    g_menu_append(menu, "Settings", "win.settings");

    GtkWidget *button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(button), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(button), G_MENU_MODEL(menu));
    g_object_unref(menu);
    return button;
}

/* ── Search / Replace / Go-to-line ────────────────────────────── */

static void search_clear_matches(NotesWindow *win) {
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(win->buffer, &s, &e);
    gtk_text_buffer_remove_tag(win->buffer, win->search_tag, &s, &e);
    g_free(win->match_lines);
    win->match_lines = NULL;
    g_free(win->match_offsets);
    win->match_offsets = NULL;
    win->match_count = 0;
    win->match_current = -1;
}

static void search_update_label(NotesWindow *win) {
    char buf[64];
    if (win->match_count == 0)
        snprintf(buf, sizeof(buf), "No results");
    else
        snprintf(buf, sizeof(buf), "%d of %d", win->match_current + 1, win->match_count);
    gtk_label_set_text(GTK_LABEL(win->match_label), buf);
}

static void search_highlight_all(NotesWindow *win) {
    search_clear_matches(win);

    const char *text = gtk_editable_get_text(GTK_EDITABLE(win->search_entry));
    if (!text || text[0] == '\0') {
        search_update_label(win);
        gtk_widget_queue_draw(win->scrollbar_overlay);
        return;
    }

    /* Collect all matches with positions for O(1) navigation */
    GArray *lines = g_array_new(FALSE, FALSE, sizeof(int));
    GArray *offsets = g_array_new(FALSE, FALSE, sizeof(int));
    GtkTextIter start;
    gtk_text_buffer_get_start_iter(win->buffer, &start);

    GtkTextIter match_start, match_end;
    while (gtk_text_iter_forward_search(&start, text,
               GTK_TEXT_SEARCH_CASE_INSENSITIVE, &match_start, &match_end, NULL)) {
        gtk_text_buffer_apply_tag(win->buffer, win->search_tag, &match_start, &match_end);
        int line = gtk_text_iter_get_line(&match_start);
        int offset = gtk_text_iter_get_offset(&match_start);
        g_array_append_val(lines, line);
        g_array_append_val(offsets, offset);
        start = match_end;
    }

    win->match_count = (int)lines->len;
    win->match_lines = (int *)g_array_free(lines, FALSE);
    win->match_offsets = (int *)g_array_free(offsets, FALSE);
    win->match_current = win->match_count > 0 ? 0 : -1;

    search_update_label(win);
    gtk_widget_queue_draw(win->scrollbar_overlay);
}

static void search_goto_match(NotesWindow *win, int idx) {
    if (win->match_count == 0 || idx < 0 || !win->match_offsets) return;

    const char *text = gtk_editable_get_text(GTK_EDITABLE(win->search_entry));
    if (!text || text[0] == '\0') return;

    /* Jump directly to stored offset — O(1) */
    GtkTextIter ms, me;
    gtk_text_buffer_get_iter_at_offset(win->buffer, &ms, win->match_offsets[idx]);
    glong search_len = g_utf8_strlen(text, -1);
    me = ms;
    gtk_text_iter_forward_chars(&me, (gint)search_len);
    gtk_text_buffer_select_range(win->buffer, &ms, &me);
    gtk_text_view_scroll_to_iter(win->text_view, &ms, 0.2, FALSE, 0, 0);
    win->match_current = idx;
    search_update_label(win);
}

static void on_search_changed(GtkEditable *editable, gpointer data) {
    (void)editable;
    NotesWindow *win = data;
    search_highlight_all(win);
    if (win->match_count > 0)
        search_goto_match(win, 0);
}

static void on_search_next(GtkWidget *widget, gpointer data) {
    (void)widget;
    NotesWindow *win = data;
    if (win->match_count == 0) return;
    int next = (win->match_current + 1) % win->match_count;
    search_goto_match(win, next);
}

static void on_search_prev(GtkWidget *widget, gpointer data) {
    (void)widget;
    NotesWindow *win = data;
    if (win->match_count == 0) return;
    int prev = (win->match_current - 1 + win->match_count) % win->match_count;
    search_goto_match(win, prev);
}

static void search_bar_close(NotesWindow *win) {
    search_clear_matches(win);
    gtk_widget_set_visible(win->search_bar, FALSE);
    gtk_widget_queue_draw(win->scrollbar_overlay);
    gtk_widget_grab_focus(GTK_WIDGET(win->text_view));
}

static void on_search_close(GtkWidget *widget, gpointer data) {
    (void)widget;
    search_bar_close(data);
}

static void on_replace_one(GtkWidget *widget, gpointer data) {
    (void)widget;
    NotesWindow *win = data;
    if (win->match_count == 0 || win->match_current < 0) return;

    const char *find = gtk_editable_get_text(GTK_EDITABLE(win->search_entry));
    const char *repl = gtk_editable_get_text(GTK_EDITABLE(win->replace_entry));
    if (!find || find[0] == '\0') return;

    /* Get current selection — it should be the current match */
    GtkTextIter sel_start, sel_end;
    if (gtk_text_buffer_get_selection_bounds(win->buffer, &sel_start, &sel_end)) {
        gtk_text_buffer_delete(win->buffer, &sel_start, &sel_end);
        gtk_text_buffer_insert(win->buffer, &sel_start, repl, -1);
    }

    search_highlight_all(win);
    if (win->match_count > 0) {
        if (win->match_current >= win->match_count)
            win->match_current = 0;
        search_goto_match(win, win->match_current);
    }
}

static void on_replace_all(GtkWidget *widget, gpointer data) {
    (void)widget;
    NotesWindow *win = data;

    const char *find = gtk_editable_get_text(GTK_EDITABLE(win->search_entry));
    const char *repl = gtk_editable_get_text(GTK_EDITABLE(win->replace_entry));
    if (!find || find[0] == '\0') return;

    GtkTextIter start;
    gtk_text_buffer_get_start_iter(win->buffer, &start);

    int replaced = 0;
    GtkTextIter ms, me;
    gtk_text_buffer_begin_user_action(win->buffer);
    while (gtk_text_iter_forward_search(&start, find,
               GTK_TEXT_SEARCH_CASE_INSENSITIVE, &ms, &me, NULL)) {
        gtk_text_buffer_delete(win->buffer, &ms, &me);
        gtk_text_buffer_insert(win->buffer, &ms, repl, -1);
        start = ms;
        replaced++;
    }
    gtk_text_buffer_end_user_action(win->buffer);

    search_highlight_all(win);

    char buf[64];
    snprintf(buf, sizeof(buf), "Replaced %d", replaced);
    gtk_label_set_text(GTK_LABEL(win->match_label), buf);
}

/* Scrollbar match markers overlay */
static void draw_scrollbar_markers(GtkDrawingArea *area, cairo_t *cr,
                                    int width, int height, gpointer data) {
    (void)area;
    NotesWindow *win = data;
    if (win->match_count == 0) return;

    int total_lines = gtk_text_buffer_get_line_count(win->buffer);
    if (total_lines <= 0) return;

    /* Determine theme-aware marker color */
    gboolean dark = is_dark_theme(win->settings.theme);
    if (dark)
        cairo_set_source_rgba(cr, 1.0, 0.7, 0.2, 0.9);
    else
        cairo_set_source_rgba(cr, 1.0, 0.5, 0.0, 0.9);

    double marker_h = 2.0;
    for (int i = 0; i < win->match_count; i++) {
        double y = ((double)win->match_lines[i] / total_lines) * height;
        cairo_rectangle(cr, 0, y, width, marker_h);
        cairo_fill(cr);
    }
}

static gboolean on_search_entry_key(GtkEventControllerKey *ctrl,
                                     guint keyval, guint keycode,
                                     GdkModifierType state, gpointer data) {
    (void)ctrl; (void)keycode;
    NotesWindow *win = data;
    if (keyval == GDK_KEY_Escape) {
        search_bar_close(win);
        return TRUE;
    }
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        if (state & GDK_SHIFT_MASK)
            on_search_prev(NULL, win);
        else
            on_search_next(NULL, win);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_replace_entry_key(GtkEventControllerKey *ctrl,
                                      guint keyval, guint keycode,
                                      GdkModifierType state, gpointer data) {
    (void)ctrl; (void)keycode; (void)state;
    NotesWindow *win = data;
    if (keyval == GDK_KEY_Escape) {
        search_bar_close(win);
        return TRUE;
    }
    return FALSE;
}

void notes_window_show_search(NotesWindow *win, gboolean with_replace) {
    gtk_widget_set_visible(win->search_bar, TRUE);
    gtk_widget_set_visible(win->replace_box, with_replace);
    gtk_widget_grab_focus(win->search_entry);

    /* Pre-fill with selected text */
    GtkTextIter sel_s, sel_e;
    if (gtk_text_buffer_get_selection_bounds(win->buffer, &sel_s, &sel_e)) {
        if (gtk_text_iter_get_line(&sel_s) == gtk_text_iter_get_line(&sel_e)) {
            char *sel = gtk_text_buffer_get_text(win->buffer, &sel_s, &sel_e, FALSE);
            gtk_editable_set_text(GTK_EDITABLE(win->search_entry), sel);
            g_free(sel);
        }
    }
    /* Select all text in the entry for quick overwrite */
    gtk_editable_select_region(GTK_EDITABLE(win->search_entry), 0, -1);
}

typedef struct { NotesWindow *win; GtkWidget *entry; GtkWidget *dialog; } GotoData;

static void on_goto_activate(GtkEntry *e, gpointer data) {
    (void)e;
    GotoData *gd = data;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(gd->entry));
    int line = atoi(text);
    if (line < 1) line = 1;
    int total = gtk_text_buffer_get_line_count(gd->win->buffer);
    if (line > total) line = total;

    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_line(gd->win->buffer, &iter, line - 1);
    gtk_text_buffer_place_cursor(gd->win->buffer, &iter);
    gtk_text_view_scroll_to_iter(gd->win->text_view, &iter, 0.2, FALSE, 0, 0);

    gtk_window_destroy(GTK_WINDOW(gd->dialog));
}

/* GotoData is freed via g_object_set_data_full on the dialog */

static gboolean on_goto_key(GtkEventControllerKey *ctrl, guint keyval,
                             guint keycode, GdkModifierType state, gpointer data) {
    (void)ctrl; (void)keycode; (void)state;
    if (keyval == GDK_KEY_Escape) {
        gtk_window_destroy(GTK_WINDOW(((GotoData *)data)->dialog));
        return TRUE;
    }
    return FALSE;
}

void notes_window_goto_line(NotesWindow *win) {
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Go to Line");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 260, -1);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    gtk_window_set_titlebar(GTK_WINDOW(dialog), adw_header_bar_new());

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(vbox, 16);
    gtk_widget_set_margin_end(vbox, 16);
    gtk_widget_set_margin_top(vbox, 16);
    gtk_widget_set_margin_bottom(vbox, 16);

    int total = gtk_text_buffer_get_line_count(win->buffer);
    char label_text[64];
    snprintf(label_text, sizeof(label_text), "Line number (1 - %d):", total);
    GtkWidget *label = gtk_label_new(label_text);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_input_purpose(GTK_ENTRY(entry), GTK_INPUT_PURPOSE_DIGITS);
    gtk_box_append(GTK_BOX(vbox), entry);

    gtk_window_set_child(GTK_WINDOW(dialog), vbox);

    GotoData *gd = g_new(GotoData, 1);
    gd->win = win;
    gd->entry = entry;
    gd->dialog = dialog;

    g_signal_connect(entry, "activate", G_CALLBACK(on_goto_activate), gd);

    GtkEventController *key = gtk_event_controller_key_new();
    g_signal_connect(key, "key-pressed", G_CALLBACK(on_goto_key), gd);
    gtk_widget_add_controller(dialog, key);

    /* Free GotoData when dialog is destroyed (covers close-without-Enter) */
    g_object_set_data_full(G_OBJECT(dialog), "goto-data", gd, g_free);

    gtk_window_present(GTK_WINDOW(dialog));
    gtk_widget_grab_focus(entry);
}

/* ── SSH/SFTP ── */

gboolean notes_window_is_remote(NotesWindow *win) {
    return win->ssh_host[0] != '\0';
}

static void update_ssh_status(NotesWindow *win) {
    gboolean connected = notes_window_is_remote(win);
    if (connected) {
        char label[512];
        snprintf(label, sizeof(label), "SSH: %s@%s", win->ssh_user, win->ssh_host);
        gtk_button_set_label(GTK_BUTTON(win->ssh_status_btn), label);
    } else {
        gtk_button_set_label(GTK_BUTTON(win->ssh_status_btn), "SSH: Off");
    }

    /* Enable/disable SSH-dependent actions */
    GAction *a;
    a = g_action_map_lookup_action(G_ACTION_MAP(win->window), "open-remote");
    if (a) g_simple_action_set_enabled(G_SIMPLE_ACTION(a), connected);
    a = g_action_map_lookup_action(G_ACTION_MAP(win->window), "sftp-disconnect");
    if (a) g_simple_action_set_enabled(G_SIMPLE_ACTION(a), connected);
}

void notes_window_ssh_connect(NotesWindow *win,
                               const char *host, const char *user,
                               int port, const char *key,
                               const char *remote_path) {
    /* Disconnect previous if any */
    if (notes_window_is_remote(win))
        notes_window_ssh_disconnect(win);

    g_strlcpy(win->ssh_host, host, sizeof(win->ssh_host));
    g_strlcpy(win->ssh_user, user, sizeof(win->ssh_user));
    win->ssh_port = port;
    g_strlcpy(win->ssh_key, key, sizeof(win->ssh_key));
    g_strlcpy(win->ssh_remote_path, remote_path, sizeof(win->ssh_remote_path));

    snprintf(win->ssh_mount, sizeof(win->ssh_mount),
             "/tmp/note-light-sftp-%d-%s@%s", (int)getpid(), user, host);

    ssh_ctl_start(win->ssh_ctl_dir, sizeof(win->ssh_ctl_dir),
                  win->ssh_ctl_path, sizeof(win->ssh_ctl_path),
                  host, user, port, key);

    update_ssh_status(win);
}

void notes_window_ssh_disconnect(NotesWindow *win) {
    if (!notes_window_is_remote(win)) return;

    ssh_ctl_stop(win->ssh_ctl_path, win->ssh_ctl_dir,
                 win->ssh_host, win->ssh_user);

    win->ssh_host[0] = '\0';
    win->ssh_user[0] = '\0';
    win->ssh_port = 0;
    win->ssh_key[0] = '\0';
    win->ssh_remote_path[0] = '\0';
    win->ssh_mount[0] = '\0';

    update_ssh_status(win);
}

/* Open remote file — called from SFTP file browser dialog */
void notes_window_open_remote_file(NotesWindow *win, const char *remote_path) {
    char *contents = NULL;
    gsize len = 0;

    if (!ssh_cat_file(win->ssh_host, win->ssh_user, win->ssh_port,
                      win->ssh_key, win->ssh_ctl_path,
                      remote_path, &contents, &len, 5 * 1024 * 1024)) {
        return;
    }

    /* Detect binary */
    gboolean is_binary = FALSE;
    gsize check = len < 8192 ? len : 8192;
    for (gsize i = 0; i < check; i++) {
        if (contents[i] == '\0') { is_binary = TRUE; break; }
    }
    if (is_binary) {
        for (gsize i = 0; i < len; i++) {
            if (contents[i] == '\0') contents[i] = '.';
        }
    }
    if (!g_utf8_validate(contents, (gssize)len, NULL)) {
        is_binary = TRUE;
        gsize bw = 0;
        char *utf8 = g_convert_with_fallback(contents, (gssize)len,
                         "UTF-8", "ISO-8859-1", ".", NULL, &bw, NULL);
        if (utf8) { g_free(contents); contents = utf8; len = bw; }
    }

    g_signal_handlers_block_by_func(win->buffer, on_buffer_changed, win);
    g_signal_handlers_block_by_func(win->buffer, on_cursor_moved, win);

    gtk_text_buffer_set_text(win->buffer, contents, (int)len);

    g_free(win->original_content);
    win->original_content = contents;
    win->original_hash = fnv1a_hash(contents, len);
    win->is_binary = is_binary;
    win->is_truncated = FALSE;
    win->dirty = FALSE;

    /* Store virtual path for save operations */
    snprintf(win->current_file, sizeof(win->current_file), "%s%s",
             win->ssh_mount, remote_path);

    char *base = g_path_get_basename(remote_path);
    char *title = g_strdup_printf("%s [%s@%s]", base, win->ssh_user, win->ssh_host);
    gtk_window_set_title(GTK_WINDOW(win->window), title);
    g_free(title);
    g_free(base);

    GtkTextIter start;
    gtk_text_buffer_get_start_iter(win->buffer, &start);
    gtk_text_buffer_place_cursor(win->buffer, &start);

    g_signal_handlers_unblock_by_func(win->buffer, on_buffer_changed, win);
    g_signal_handlers_unblock_by_func(win->buffer, on_cursor_moved, win);

    update_cursor_position(win);
    if (win->settings.show_line_numbers)
        update_line_numbers(win->buffer, win);
    update_line_highlights(win);
    apply_font_intensity(win);

    char status[128];
    snprintf(status, sizeof(status), "UTF-8 | %s | remote", is_binary ? "BIN" : "TEXT");
    gtk_label_set_text(win->status_encoding, status);
}

/* Save to remote file */
gboolean save_remote_file(NotesWindow *win) {
    if (!notes_window_is_remote(win)) return FALSE;
    if (!ssh_path_is_remote(win->current_file)) return FALSE;

    char remote[4096];
    ssh_to_remote_path(win->ssh_mount, win->ssh_remote_path,
                       win->current_file, remote, sizeof(remote));

    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(win->buffer, &s, &e);
    char *text = gtk_text_buffer_get_text(win->buffer, &s, &e, FALSE);
    gsize len = strlen(text);

    gboolean ok = ssh_write_file(win->ssh_host, win->ssh_user, win->ssh_port,
                                  win->ssh_key, win->ssh_ctl_path,
                                  remote, text, len);

    if (ok) {
        g_free(win->original_content);
        win->original_content = text;
        win->original_hash = fnv1a_hash(text, len);
        win->dirty = FALSE;

        char *base = g_path_get_basename(remote);
        char *title = g_strdup_printf("%s [%s@%s]", base, win->ssh_user, win->ssh_host);
        gtk_window_set_title(GTK_WINDOW(win->window), title);
        g_free(title);
        g_free(base);
    } else {
        g_free(text);
    }

    return ok;
}

static gboolean on_editor_ctrl_scroll(GtkEventControllerScroll *ctrl,
                                      double dx, double dy, gpointer user) {
    (void)dx;
    GdkModifierType mods = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(ctrl));
    if (!(mods & GDK_CONTROL_MASK)) return FALSE;
    NotesWindow *win = user;
    if (dy < 0 && win->settings.font_size < 72)
        win->settings.font_size += 1;
    else if (dy > 0 && win->settings.font_size > 6)
        win->settings.font_size -= 1;
    else
        return TRUE;
    notes_window_apply_settings(win);
    settings_save(&win->settings);
    return TRUE;
}

NotesWindow *notes_window_new(GtkApplication *app) {
    NotesWindow *win = g_new0(NotesWindow, 1);
    settings_load(&win->settings);

    win->window = GTK_APPLICATION_WINDOW(gtk_application_window_new(app));
    gtk_window_set_title(GTK_WINDOW(win->window), "Notes MD");
    gtk_window_set_default_size(GTK_WINDOW(win->window),
                                win->settings.window_width,
                                win->settings.window_height);

    g_signal_connect(win->window, "close-request", G_CALLBACK(on_close_request), win);
    g_signal_connect(win->window, "destroy", G_CALLBACK(on_destroy), win);

    /* Header bar */
    GtkWidget *header = gtk_header_bar_new();
    GtkWidget *menu_btn = build_menu_button();
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu_btn);
    gtk_window_set_titlebar(GTK_WINDOW(win->window), header);

    /* Line numbers */
    win->line_numbers = GTK_DRAWING_AREA(gtk_drawing_area_new());
    gtk_widget_set_can_focus(GTK_WIDGET(win->line_numbers), FALSE);
    gtk_widget_add_css_class(GTK_WIDGET(win->line_numbers), "line-numbers");
    gtk_drawing_area_set_draw_func(win->line_numbers, draw_line_numbers, win, NULL);

    /* Source buffer + text view (custom subclass) */
    win->source_buffer = gtk_source_buffer_new(NULL);
    win->buffer = GTK_TEXT_BUFFER(win->source_buffer);
    NotesTextView *ntv = g_object_new(NOTES_TYPE_TEXT_VIEW, "buffer", win->buffer, NULL);
    ntv->win = win;
    win->source_view = GTK_SOURCE_VIEW(ntv);
    win->text_view = GTK_TEXT_VIEW(ntv);
    gtk_text_view_set_wrap_mode(win->text_view, GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(win->text_view, 12);
    gtk_text_view_set_right_margin(win->text_view, 12);
    gtk_text_view_set_top_margin(win->text_view, 8);
    gtk_text_view_set_bottom_margin(win->text_view, 8);

    /* Search highlight tag */
    win->search_tag = gtk_text_buffer_create_tag(win->buffer, "search-match",
                                                  "background", "#f0b030",
                                                  "foreground", "#000000", NULL);

    /* Font intensity tag */
    win->intensity_tag = gtk_text_buffer_create_tag(win->buffer, "intensity",
                                                     "foreground-rgba", NULL, NULL);

    g_signal_connect(win->buffer, "changed", G_CALLBACK(on_buffer_changed), win);
    g_signal_connect(win->buffer, "notify::cursor-position", G_CALLBACK(on_cursor_moved), win);

    /* Ctrl+scroll zoom on editor */
    GtkEventController *editor_scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(editor_scroll, "scroll", G_CALLBACK(on_editor_ctrl_scroll), win);
    gtk_widget_add_controller(GTK_WIDGET(win->text_view), editor_scroll);

    /* Scrolled window for text view */
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), GTK_WIDGET(win->text_view));
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_widget_set_hexpand(scrolled, TRUE);

    /* Line numbers container */
    win->ln_scrolled = GTK_WIDGET(win->line_numbers);
    gtk_widget_set_vexpand(win->ln_scrolled, TRUE);

    /* Redraw line numbers when the main text view scrolls */
    GtkAdjustment *main_vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled));
    g_signal_connect_swapped(main_vadj, "value-changed",
                             G_CALLBACK(gtk_widget_queue_draw), win->line_numbers);

    win->scrolled_window = scrolled;

    /* Scrollbar match markers overlay — sits on right edge of scrolled window */
    GtkWidget *scroll_overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(scroll_overlay), scrolled);

    win->scrollbar_overlay = gtk_drawing_area_new();
    gtk_widget_set_halign(win->scrollbar_overlay, GTK_ALIGN_END);
    gtk_widget_set_size_request(win->scrollbar_overlay, 6, -1);
    gtk_widget_set_can_target(win->scrollbar_overlay, FALSE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(win->scrollbar_overlay),
                                    draw_scrollbar_markers, win, NULL);
    gtk_overlay_add_overlay(GTK_OVERLAY(scroll_overlay), win->scrollbar_overlay);

    /* HBox: line numbers + text view */
    win->editor_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(win->editor_box), win->ln_scrolled);
    gtk_box_append(GTK_BOX(win->editor_box), scroll_overlay);

    /* Status bar */
    GtkWidget *status_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(status_bar, "statusbar");
    gtk_widget_set_margin_start(status_bar, 8);
    gtk_widget_set_margin_end(status_bar, 8);
    gtk_widget_set_margin_top(status_bar, 2);
    gtk_widget_set_margin_bottom(status_bar, 2);

    win->status_encoding = GTK_LABEL(gtk_label_new("UTF-8"));
    gtk_widget_set_halign(GTK_WIDGET(win->status_encoding), GTK_ALIGN_START);
    gtk_widget_set_hexpand(GTK_WIDGET(win->status_encoding), TRUE);
    gtk_box_append(GTK_BOX(status_bar), GTK_WIDGET(win->status_encoding));

    win->ssh_status_btn = gtk_button_new_with_label("SSH: Off");
    gtk_widget_add_css_class(win->ssh_status_btn, "flat");
    gtk_widget_set_halign(win->ssh_status_btn, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(status_bar), win->ssh_status_btn);

    win->status_cursor = GTK_LABEL(gtk_label_new("Ln 1, Col 1"));
    gtk_widget_set_halign(GTK_WIDGET(win->status_cursor), GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(status_bar), GTK_WIDGET(win->status_cursor));

    /* Search bar */
    win->search_bar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(win->search_bar, 8);
    gtk_widget_set_margin_end(win->search_bar, 8);
    gtk_widget_set_margin_top(win->search_bar, 4);
    gtk_widget_set_margin_bottom(win->search_bar, 4);
    gtk_widget_set_visible(win->search_bar, FALSE);

    /* Search row */
    GtkWidget *search_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    win->search_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(win->search_entry), "Find...");
    gtk_widget_set_hexpand(win->search_entry, TRUE);
    g_signal_connect(win->search_entry, "changed", G_CALLBACK(on_search_changed), win);

    GtkEventController *search_key = gtk_event_controller_key_new();
    g_signal_connect(search_key, "key-pressed", G_CALLBACK(on_search_entry_key), win);
    gtk_widget_add_controller(win->search_entry, search_key);

    GtkWidget *prev_btn = gtk_button_new_from_icon_name("go-up-symbolic");
    GtkWidget *next_btn = gtk_button_new_from_icon_name("go-down-symbolic");
    g_signal_connect(prev_btn, "clicked", G_CALLBACK(on_search_prev), win);
    g_signal_connect(next_btn, "clicked", G_CALLBACK(on_search_next), win);

    win->match_label = gtk_label_new("");
    gtk_widget_set_size_request(win->match_label, 80, -1);

    GtkWidget *close_btn = gtk_button_new_from_icon_name("window-close-symbolic");
    g_signal_connect(close_btn, "clicked", G_CALLBACK(on_search_close), win);

    gtk_box_append(GTK_BOX(search_row), win->search_entry);
    gtk_box_append(GTK_BOX(search_row), prev_btn);
    gtk_box_append(GTK_BOX(search_row), next_btn);
    gtk_box_append(GTK_BOX(search_row), win->match_label);
    gtk_box_append(GTK_BOX(search_row), close_btn);
    gtk_box_append(GTK_BOX(win->search_bar), search_row);

    /* Replace row */
    win->replace_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    win->replace_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(win->replace_entry), "Replace...");
    gtk_widget_set_hexpand(win->replace_entry, TRUE);

    GtkEventController *replace_key = gtk_event_controller_key_new();
    g_signal_connect(replace_key, "key-pressed", G_CALLBACK(on_replace_entry_key), win);
    gtk_widget_add_controller(win->replace_entry, replace_key);

    GtkWidget *repl_btn = gtk_button_new_with_label("Replace");
    GtkWidget *repl_all_btn = gtk_button_new_with_label("All");
    g_signal_connect(repl_btn, "clicked", G_CALLBACK(on_replace_one), win);
    g_signal_connect(repl_all_btn, "clicked", G_CALLBACK(on_replace_all), win);

    gtk_box_append(GTK_BOX(win->replace_box), win->replace_entry);
    gtk_box_append(GTK_BOX(win->replace_box), repl_btn);
    gtk_box_append(GTK_BOX(win->replace_box), repl_all_btn);
    gtk_box_append(GTK_BOX(win->search_bar), win->replace_box);
    gtk_widget_set_visible(win->replace_box, FALSE);

    /* Main vbox: search + editor + statusbar */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(win->editor_box, TRUE);
    gtk_box_append(GTK_BOX(vbox), win->search_bar);
    gtk_box_append(GTK_BOX(vbox), win->editor_box);
    gtk_box_append(GTK_BOX(vbox), status_bar);

    gtk_window_set_child(GTK_WINDOW(win->window), vbox);

    /* CSS provider */
    win->css_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(win->css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    /* Actions & shortcuts */
    actions_setup(win, app);

    /* Disable SSH-dependent actions until connected */
    update_ssh_status(win);

    /* Apply settings */
    notes_window_apply_settings(win);

    /* Markdown preview (WebKit) — creates paned wrapper, hidden by default */
    preview_init(win);

    /* Restore last file */
    if (win->settings.last_file[0] != '\0')
        notes_window_load_file(win, win->settings.last_file);

    return win;
}
