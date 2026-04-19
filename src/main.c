#include <adwaita.h>
#include "window.h"

static gboolean restore_title_cb(gpointer data) {
    NotesWindow *win = data;
    win->title_idle_id = 0;
    if (win->current_file[0] != '\0') {
        char *base = g_path_get_basename(win->current_file);
        gtk_window_set_title(GTK_WINDOW(win->window), base);
        g_free(base);
    }
    return G_SOURCE_REMOVE;
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    NotesWindow *win = notes_window_new(app);
    gtk_window_present(GTK_WINDOW(win->window));

    /* Re-set title after present — GTK/Adw theme changes can clear it */
    if (win->current_file[0] != '\0')
        win->title_idle_id = g_idle_add(restore_title_cb, win);
}

static void on_open(GApplication *app, GFile **files, gint n_files,
                    const gchar *hint, gpointer user_data) {
    (void)hint; (void)user_data;
    for (gint i = 0; i < n_files; i++) {
        NotesWindow *win = notes_window_new(GTK_APPLICATION(app));
        char *path = g_file_get_path(files[i]);
        if (path) {
            notes_window_load_file(win, path);
            g_free(path);
        }
        gtk_window_present(GTK_WINDOW(win->window));
        if (win->current_file[0] != '\0')
            win->title_idle_id = g_idle_add(restore_title_cb, win);
    }
}

static void on_quit(GSimpleAction *action, GVariant *param, gpointer app) {
    (void)action; (void)param;
    g_application_quit(G_APPLICATION(app));
}

int main(int argc, char *argv[]) {
    gtk_source_init();
    AdwApplication *app = adw_application_new("com.notes.md",
                                               G_APPLICATION_HANDLES_OPEN);

    static const GActionEntry app_entries[] = {
        {"quit", on_quit, NULL, NULL, NULL, {0}},
    };

    g_action_map_add_action_entries(G_ACTION_MAP(app), app_entries,
                                   G_N_ELEMENTS(app_entries), app);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    g_signal_connect(app, "open", G_CALLBACK(on_open), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    gtk_source_finalize();
    return status;
}
