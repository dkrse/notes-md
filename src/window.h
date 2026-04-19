#ifndef NOTES_WINDOW_H
#define NOTES_WINDOW_H

#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>
#include "settings.h"

typedef struct {
    GtkApplicationWindow *window;
    GtkSourceView        *source_view;
    GtkSourceBuffer      *source_buffer;
    GtkTextView          *text_view;     /* alias for source_view */
    GtkTextBuffer        *buffer;        /* alias for source_buffer */
    GtkDrawingArea       *line_numbers;
    GtkWidget            *ln_scrolled;
    GtkWidget            *editor_box;
    int                   highlight_line;
    GdkRGBA               highlight_rgba;
    GtkTextTag           *intensity_tag;
    GtkLabel             *status_encoding;
    GtkLabel             *status_cursor;
    NotesSettings         settings;
    GtkCssProvider       *css_provider;
    char                  current_file[2048];
    int                   cached_line_count;
    guint                 line_numbers_idle_id;
    guint                 intensity_idle_id;
    guint                 scroll_idle_id;
    guint                 title_idle_id;
    gboolean              dirty;
    gboolean              is_binary;
    gboolean              is_truncated;
    char                 *original_content;
    guint32               original_hash;

    /* SSH/SFTP state */
    char                  ssh_host[256];
    char                  ssh_user[128];
    int                   ssh_port;
    char                  ssh_key[1024];
    char                  ssh_remote_path[1024];
    char                  ssh_mount[2048];
    char                  ssh_ctl_path[512];
    char                  ssh_ctl_dir[256];
    GtkWidget            *ssh_status_btn;

    /* Search/Replace */
    GtkWidget            *search_bar;
    GtkWidget            *search_entry;
    GtkWidget            *replace_entry;
    GtkWidget            *replace_box;
    GtkWidget            *match_label;
    GtkTextTag           *search_tag;
    GtkWidget            *scrolled_window;
    GtkWidget            *scrollbar_overlay;
    int                  *match_lines;
    int                  *match_offsets;  /* byte offsets of each match start */
    int                   match_count;
    int                   match_current;

    /* Markdown preview */
    GtkWidget            *preview_stack;
    GtkWidget            *preview_switcher;
    GtkWidget            *preview_webview;
    gboolean              preview_visible;
    gboolean              preview_ready;
    guint                 preview_update_id;
    double                preview_zoom;
} NotesWindow;

guint32      fnv1a_hash(const char *data, gsize len);
NotesWindow *notes_window_new(GtkApplication *app);
void         notes_window_apply_settings(NotesWindow *win);
void         notes_window_load_file(NotesWindow *win, const char *path);
void         notes_window_show_search(NotesWindow *win, gboolean with_replace);
void         notes_window_goto_line(NotesWindow *win);
void         notes_window_ssh_connect(NotesWindow *win,
                                       const char *host, const char *user,
                                       int port, const char *key,
                                       const char *remote_path);
void         notes_window_ssh_disconnect(NotesWindow *win);
gboolean     notes_window_is_remote(NotesWindow *win);
gboolean     save_remote_file(NotesWindow *win);
void         notes_window_open_remote_file(NotesWindow *win, const char *remote_path);

#endif
