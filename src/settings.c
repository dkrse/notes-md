#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <glib/gstdio.h>

static void ensure_config_dir(void) {
    char path[1024];
    const char *config = g_get_user_config_dir();
    snprintf(path, sizeof(path), "%s/notes-md", config);
    g_mkdir_with_parents(path, 0755);
}

char *settings_get_config_path(void) {
    static char path[1024];
    const char *config = g_get_user_config_dir();
    snprintf(path, sizeof(path), "%s/notes-md/settings.conf", config);
    return path;
}

void settings_load(NotesSettings *s) {
    /* defaults */
    strncpy(s->font, "Monospace", sizeof(s->font) - 1);
    s->font_size = 14;
    strncpy(s->gui_font, "Sans", sizeof(s->gui_font) - 1);
    s->gui_font_size = 10;
    s->font_intensity = 1.0;
    s->line_spacing = 1.0;
    strncpy(s->theme, "system", sizeof(s->theme) - 1);
    s->show_line_numbers = FALSE;
    s->highlight_current_line = TRUE;
    s->wrap_lines = TRUE;
    s->highlight_syntax = TRUE;
    s->preview_full_width = FALSE;
    s->preview_font_size = 14;
    s->watch_file = TRUE;
    s->disable_gpu = TRUE;
    s->pdf_margin_top = 20.0;
    s->pdf_margin_bottom = 20.0;
    s->pdf_margin_left = 20.0;
    s->pdf_margin_right = 20.0;
    s->pdf_landscape = FALSE;
    strncpy(s->pdf_page_numbers, "none", sizeof(s->pdf_page_numbers) - 1);
    s->window_width = 700;
    s->window_height = 500;
    s->last_file[0] = '\0';

    char *path = settings_get_config_path();
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = g_strstrip(line);
        char *val = g_strstrip(eq + 1);

        #define SAFE_COPY(dst, src) do { \
            strncpy((dst), (src), sizeof(dst) - 1); \
            (dst)[sizeof(dst) - 1] = '\0'; \
        } while (0)

        /* Replace comma with dot for locale-safe float parsing */
        for (char *c = val; *c; c++) { if (*c == ',') *c = '.'; }

        if (strcmp(key, "font") == 0) {
            if (val[0]) SAFE_COPY(s->font, val);
        } else if (strcmp(key, "font_size") == 0) {
            int v = atoi(val); if (v >= 6 && v <= 72) s->font_size = v;
        } else if (strcmp(key, "gui_font") == 0) {
            if (val[0]) SAFE_COPY(s->gui_font, val);
        } else if (strcmp(key, "gui_font_size") == 0) {
            int v = atoi(val); if (v >= 6 && v <= 72) s->gui_font_size = v;
        } else if (strcmp(key, "font_intensity") == 0)
            s->font_intensity = CLAMP(g_ascii_strtod(val, NULL), 0.3, 1.0);
        else if (strcmp(key, "line_spacing") == 0) {
            double v = g_ascii_strtod(val, NULL); if (v >= 0.5 && v <= 5.0) s->line_spacing = v;
        }
        else if (strcmp(key, "theme") == 0) {
            if (val[0]) SAFE_COPY(s->theme, val);
        }
        else if (strcmp(key, "show_line_numbers") == 0)
            s->show_line_numbers = (strcmp(val, "1") == 0);
        else if (strcmp(key, "highlight_current_line") == 0)
            s->highlight_current_line = (strcmp(val, "1") == 0);
        else if (strcmp(key, "wrap_lines") == 0)
            s->wrap_lines = (strcmp(val, "1") == 0);
        else if (strcmp(key, "highlight_syntax") == 0)
            s->highlight_syntax = (strcmp(val, "1") == 0);
        else if (strcmp(key, "preview_full_width") == 0)
            s->preview_full_width = (strcmp(val, "1") == 0);
        else if (strcmp(key, "preview_font_size") == 0) {
            int v = atoi(val); if (v >= 6 && v <= 72) s->preview_font_size = v;
        }
        else if (strcmp(key, "watch_file") == 0)
            s->watch_file = (strcmp(val, "1") == 0);
        else if (strcmp(key, "disable_gpu") == 0)
            s->disable_gpu = (strcmp(val, "1") == 0);
        else if (strcmp(key, "pdf_margin_top") == 0)
            s->pdf_margin_top = CLAMP(g_ascii_strtod(val, NULL), 0.0, 100.0);
        else if (strcmp(key, "pdf_margin_bottom") == 0)
            s->pdf_margin_bottom = CLAMP(g_ascii_strtod(val, NULL), 0.0, 100.0);
        else if (strcmp(key, "pdf_margin_left") == 0)
            s->pdf_margin_left = CLAMP(g_ascii_strtod(val, NULL), 0.0, 100.0);
        else if (strcmp(key, "pdf_margin_right") == 0)
            s->pdf_margin_right = CLAMP(g_ascii_strtod(val, NULL), 0.0, 100.0);
        else if (strcmp(key, "pdf_landscape") == 0)
            s->pdf_landscape = (strcmp(val, "1") == 0);
        else if (strcmp(key, "pdf_page_numbers") == 0) {
            if (val[0]) SAFE_COPY(s->pdf_page_numbers, val);
        }
        else if (strcmp(key, "window_width") == 0) {
            int v = atoi(val); if (v >= 200 && v <= 8192) s->window_width = v;
        } else if (strcmp(key, "window_height") == 0) {
            int v = atoi(val); if (v >= 200 && v <= 8192) s->window_height = v;
        }
        else if (strcmp(key, "last_file") == 0)
            SAFE_COPY(s->last_file, val);

        #undef SAFE_COPY
    }
    fclose(f);
}

/* ── SFTP Connections ── */

static char *connections_get_path(void) {
    static char path[1024];
    const char *config = g_get_user_config_dir();
    snprintf(path, sizeof(path), "%s/notes-md/connections.conf", config);
    return path;
}

void connections_load(SftpConnections *c) {
    memset(c, 0, sizeof(*c));
    FILE *f = fopen(connections_get_path(), "r");
    if (!f) return;

    char line[4096];
    int idx = -1;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '[') {
            idx++;
            if (idx >= MAX_CONNECTIONS) break;
            c->count = idx + 1;
            char *end = strchr(line, ']');
            if (end) *end = '\0';
            g_strlcpy(c->items[idx].name, line + 1, sizeof(c->items[idx].name));
            c->items[idx].port = 22;
            continue;
        }
        if (idx < 0 || idx >= MAX_CONNECTIONS) continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line, *val = eq + 1;
        SftpConnection *s = &c->items[idx];
        if (strcmp(key, "host") == 0) g_strlcpy(s->host, val, sizeof(s->host));
        else if (strcmp(key, "port") == 0) s->port = atoi(val);
        else if (strcmp(key, "user") == 0) g_strlcpy(s->user, val, sizeof(s->user));
        else if (strcmp(key, "remote_path") == 0) g_strlcpy(s->remote_path, val, sizeof(s->remote_path));
        else if (strcmp(key, "use_key") == 0) s->use_key = atoi(val);
        else if (strcmp(key, "key_path") == 0) g_strlcpy(s->key_path, val, sizeof(s->key_path));
    }
    fclose(f);
}

void connections_save(const SftpConnections *c) {
    ensure_config_dir();
    char *path = connections_get_path();

    /* Atomic write: write to exclusive tmp, then rename */
    char tmp[1088];
    snprintf(tmp, sizeof(tmp), "%s.XXXXXX", path);
    int fd = g_mkstemp(tmp);
    if (fd < 0) return;
    fchmod(fd, 0600);
    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); g_remove(tmp); return; }

    for (int i = 0; i < c->count; i++) {
        const SftpConnection *s = &c->items[i];
        fprintf(f, "[%s]\n", s->name);
        fprintf(f, "host=%s\n", s->host);
        fprintf(f, "port=%d\n", s->port);
        fprintf(f, "user=%s\n", s->user);
        fprintf(f, "remote_path=%s\n", s->remote_path);
        fprintf(f, "use_key=%d\n", s->use_key);
        fprintf(f, "key_path=%s\n", s->key_path);
        fprintf(f, "\n");
    }

    gboolean ok = (fflush(f) == 0);
    if (fclose(f) != 0) ok = FALSE;
    if (ok)
        g_rename(tmp, path);
    else
        g_remove(tmp);
}

void settings_save(const NotesSettings *s) {
    ensure_config_dir();
    char *path = settings_get_config_path();

    /* Atomic write: write to exclusive tmp, then rename */
    char tmp[1088];
    snprintf(tmp, sizeof(tmp), "%s.XXXXXX", path);
    int fd = g_mkstemp(tmp);
    if (fd < 0) return;
    fchmod(fd, 0600);
    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); g_remove(tmp); return; }

    fprintf(f, "font=%s\n", s->font);
    fprintf(f, "font_size=%d\n", s->font_size);
    fprintf(f, "gui_font=%s\n", s->gui_font);
    fprintf(f, "gui_font_size=%d\n", s->gui_font_size);
    char buf_intensity[32], buf_spacing[32];
    g_ascii_formatd(buf_intensity, sizeof(buf_intensity), "%.2f", s->font_intensity);
    g_ascii_formatd(buf_spacing, sizeof(buf_spacing), "%.1f", s->line_spacing);
    fprintf(f, "font_intensity=%s\n", buf_intensity);
    fprintf(f, "line_spacing=%s\n", buf_spacing);
    fprintf(f, "theme=%s\n", s->theme);
    fprintf(f, "show_line_numbers=%d\n", s->show_line_numbers);
    fprintf(f, "highlight_current_line=%d\n", s->highlight_current_line);
    fprintf(f, "wrap_lines=%d\n", s->wrap_lines);
    fprintf(f, "highlight_syntax=%d\n", s->highlight_syntax);
    fprintf(f, "preview_full_width=%d\n", s->preview_full_width);
    fprintf(f, "preview_font_size=%d\n", s->preview_font_size);
    fprintf(f, "watch_file=%d\n", s->watch_file);
    fprintf(f, "disable_gpu=%d\n", s->disable_gpu);
    char buf_mt[32], buf_mb[32], buf_ml[32], buf_mr[32];
    g_ascii_formatd(buf_mt, sizeof(buf_mt), "%.1f", s->pdf_margin_top);
    g_ascii_formatd(buf_mb, sizeof(buf_mb), "%.1f", s->pdf_margin_bottom);
    g_ascii_formatd(buf_ml, sizeof(buf_ml), "%.1f", s->pdf_margin_left);
    g_ascii_formatd(buf_mr, sizeof(buf_mr), "%.1f", s->pdf_margin_right);
    fprintf(f, "pdf_margin_top=%s\n", buf_mt);
    fprintf(f, "pdf_margin_bottom=%s\n", buf_mb);
    fprintf(f, "pdf_margin_left=%s\n", buf_ml);
    fprintf(f, "pdf_margin_right=%s\n", buf_mr);
    fprintf(f, "pdf_landscape=%d\n", s->pdf_landscape);
    fprintf(f, "pdf_page_numbers=%s\n", s->pdf_page_numbers);
    fprintf(f, "window_width=%d\n", s->window_width);
    fprintf(f, "window_height=%d\n", s->window_height);
    fprintf(f, "last_file=%s\n", s->last_file);

    gboolean ok = (fflush(f) == 0);
    if (fclose(f) != 0) ok = FALSE;
    if (ok)
        g_rename(tmp, path);
    else
        g_remove(tmp);
}
