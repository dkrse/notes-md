#ifndef NOTES_PREVIEW_H
#define NOTES_PREVIEW_H

#include "window.h"

void     preview_init(NotesWindow *win);
void     preview_toggle(NotesWindow *win);
gboolean preview_is_visible(NotesWindow *win);
void     preview_queue_update(NotesWindow *win);
void     preview_apply_theme(NotesWindow *win);
void     preview_apply_layout(NotesWindow *win);
void     preview_zoom_step(NotesWindow *win, double delta);

#endif
