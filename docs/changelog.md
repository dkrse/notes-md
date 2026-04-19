# Changelog

## 2026-04-19

### Added
- `preview_full_width` setting — toggle in Settings dialog to switch the markdown preview between centered (default) and left-aligned / full-width. Persists to `settings.conf`.
- Ctrl + mouse wheel zoom on the editor (adjusts `font_size`, range 6–72).
- Ctrl + mouse wheel zoom on the preview (adjusts WebKit zoom level via `preview_zoom_step`, CAPTURE phase so WebKit doesn't swallow the event).
- Documentation: `README.md`, `docs/architecture.md`, `docs/changelog.md`.

### Fixed
- Title buffer overflows in three `gtk_window_set_title` paths (`window.c`: dirty-state update, remote open, remote save). Replaced fixed 512-byte stack buffers with `g_strdup_printf` — adversarial `ssh_host` (256) + `ssh_user` (128) + basename could previously exceed the buffer.
- `fread` in `notes_window_load_file` now checks `ferror(fp)` and aborts cleanly on read failure instead of treating garbage as file contents.
- `settings_save` now uses `g_mkstemp` + `fchmod 0600` + atomic `g_rename` (same pattern as `connections_save`), eliminating a symlink race in the previous `g_open(O_CREAT)` path and unifying the atomic-write code path.
- `fclose` error now invalidates the atomic write in `settings_save` and `connections_save` (previously silently ignored).
- `snprintf` truncation in `actions.c` remote file browser label and full-path composition replaced with `g_strdup_printf`.
- Defensive `len` check before `lines[i][len-1]` in SFTP directory parser (`actions.c`).

### Build
- `Makefile`: added `-MMD -MP` and `-include $(DEP)` so header changes correctly trigger recompilation of dependent objects. Previous builds could leave `.o` files compiled against an older `NotesSettings` layout after `settings.h` grew a field, producing wild offsets (observed as `Invalid utf8 passed to gdk_surface_set_title` followed by SIGSEGV). Running `make clean && make` would restore correctness; the new dependency tracking makes `make` alone sufficient.

### Not changed (deliberate)
- `StrictHostKeyChecking=accept-new` remains. It is OpenSSH's recommended TOFU mode (accepts unknown hosts once, rejects changed host keys). Moving to `ask` would be incompatible with the non-interactive `BatchMode=yes` used by the helper commands.
- `mkdtemp`-created SSH control directory is not considered a symlink risk — `mkdtemp` atomically creates a 0700 directory with a unique randomised name.
