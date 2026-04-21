# Architecture

## Overview

notes-md is a GTK 4 + libadwaita desktop application written in C17. The codebase is ~12k lines split across six translation units under `src/`.

```
┌──────────┐   ┌───────────┐   ┌────────────┐
│  main.c  │──▶│ window.c  │──▶│ actions.c  │
└──────────┘   │           │   └────────────┘
               │           │   ┌────────────┐
               │           │──▶│ preview.c  │──▶ WebKit
               │           │   └────────────┘
               │           │   ┌────────────┐
               │           │──▶│  ssh.c     │──▶ OpenSSH (ControlMaster)
               │           │   └────────────┘
               │           │   ┌────────────┐
               │           │──▶│ settings.c │──▶ ~/.config/notes-md
               └───────────┘   └────────────┘
```

## Modules

### `main.c`
`AdwApplication` entry point. Handles `activate` and `open` signals, creates `NotesWindow` instances, wires the `quit` action, and queues a delayed title restore (GTK sometimes clears the title during theme negotiation).

### `window.c`
Owns the `NotesWindow` struct and the majority of UI construction:

- Custom `NotesTextView` (subclass of `GtkSourceView`) for snapshot-time line highlights.
- GtkSourceBuffer with a markdown language spec, search highlight tag, font-intensity tag.
- Line-number drawing area synchronised to the scrolled window's vadjustment.
- Scrollbar-marker overlay (`draw_scrollbar_markers`) for search matches.
- Theme/CSS application pipeline (`apply_theme`, `apply_source_style`, `apply_css`).
- File load path (`notes_window_load_file`) with a 5 MB display cap and binary detection.
- External file watch (`start_file_watch` / `reload_with_cursor`) using `GFileMonitor`. Reloads on disk changes when the buffer is clean and the on-disk content hash differs from `original_hash`; cursor line is preserved. Skipped for SFTP mounts.
- SSH remote open/save wrappers.
- Dirty-state tracking via FNV-1a hash of buffer text against `original_content`.
- Ctrl+scroll zoom controller for the editor.

### `actions.c`
Menu/action callbacks and modal dialogs:

- File: new, open, save, save-as, quit.
- Edit: find, replace, goto line, zoom in/out.
- Settings dialog: font pickers, theme dropdown, line-spacing dropdown, line-numbers / current-line / wrap / syntax / preview-full-width / reload-on-file-change / disable-preview-gpu toggles, font-intensity slider, preview font size.
- SFTP connection manager and remote file browser.

### `preview.c`
WebKit-based live markdown preview:

- Loads `data/webview/preview.html` which exposes `window.nmdRender`, `nmdSetTheme`, `nmdSetLayout`.
- `push_text` / `preview_queue_update` throttle buffer → preview updates via a 250 ms `g_timeout`.
- `preview_apply_theme` follows Adwaita's dark mode or the user's theme override.
- `preview_apply_layout` toggles `html.full` (left-aligned) vs centered.
- `preview_apply_font_size` sets WebKit zoom level from `settings.preview_font_size`.
- GPU compositing honours `settings.disable_gpu`: when set, `webkit_settings_set_hardware_acceleration_policy(…NEVER)` forces software rendering. Works around nvidia/Wayland GBM buffer failures where the DOM is populated but the view is never composited. Change requires an app restart (setting is read once at webview creation).
- Find bar (GtkBox at top of preview page) driven by `WebKitFindController`, shown on Ctrl+F when preview is visible.
- `preview_export_pdf` injects a `@media print { @page { @bottom-center { content: … } } }` style tag based on `pdf_page_numbers` setting, then drives `webkit_print_operation_print` with a `GtkPageSetup` built from margin/landscape settings.
- Ctrl+scroll zoom controller on the webview (CAPTURE phase to preempt WebKit's own handler); persists to `preview_font_size`.

### `ssh.c`
Wraps the OpenSSH CLI:

- `ssh_arg_is_safe()` validates host/user inputs — rejects empty strings, leading `-` (would be parsed as an ssh option), and control characters. `ssh_argv_new` returns `NULL` on invalid input; callers abort the operation.
- `ssh_ctl_start` / `ssh_ctl_stop` manage a per-session `ControlMaster` socket under `$XDG_RUNTIME_DIR/notes-ssh-XXXXXX/ctl` (created with `mkdtemp` so the directory is 0700 and unique).
- `ssh_argv_new` builds argument vectors for multiplexed `ssh` invocations (`BatchMode=yes`, `StrictHostKeyChecking=accept-new`). Publickey-only; no password auth path exists.
- `ssh_cat_file` / `ssh_write_file` read/write remote files via `cat` / `tee` over the control socket.
- Path helpers translate between virtual mount paths (`/note-light-sftp-*/…`) and real remote paths.

### `settings.c`
Flat key=value config files in `~/.config/notes-md/`:

- `settings.conf` — editor preferences and last-opened file.
- `connections.conf` — named SFTP connection profiles (`[name]` sections).
- Atomic saves: write to `mkstemp`-created `*.XXXXXX` file with 0600 perms, `fflush` + `fclose` error-checked, then `g_rename`.

## Data flow

### Local file open
`actions.c` file-dialog → `notes_window_load_file` → `fopen`/`fread` (5 MB cap, `ferror` check) → binary-detect → GtkTextBuffer → hash stored → title set.

### Remote file open
`actions.c` browser picks path → `notes_window_open_remote_file` → `ssh_cat_file` → same buffer load path → title includes `[user@host]`.

### Save
`save_local_file` / `save_remote_file` write bytes, recompute `original_hash`, clear `dirty`, update title.

### Preview
Every buffer change in preview-visible mode schedules `update_timeout_cb` (250 ms debounce) → JSON-escape buffer text → `webkit_web_view_evaluate_javascript("window.nmdRender(...)")`. `nmdRender` dispatches to the math engine picked at boot:

- **KaTeX path**: protect math spans with `%%NMD_…%%` placeholders, run `marked`, synchronously substitute `katex.renderToString` HTML back in, single `el.innerHTML = …` write, then Mermaid. One layout point — off-screen blocks paint consistently on software-rendered WebKit.
- **MathJax path**: register `mathBlock` / `mathInline` as marked extensions (tokenizer-level capture, so CommonMark backslash-escape can't swallow `\[ \]` / `\( \)` delimiters), run `marked.parse` (extensions emit `\[…\]` / `\(…\)` into the HTML), set `el.innerHTML`, run Mermaid, then `await MathJax.typesetPromise([el])`. Followed by a `display:none` → reflow → `display:''` repaint nudge to compensate for async typeset on software WebKit. A stub file at `data/webview/mathjax/output/svg/fonts/tex.js` satisfies a dependency fetch that the `tex-chtml` bundle issues under `POLICY_NEVER` — without it `MathJax.loader.load` rejects and `typesetPromise` hangs forever.

The engine is selected from `settings.math_engine` and passed to the webview via the `?engine=` URL query; `preview.js` reads it on bootstrap and loads only the chosen library.

### External file reload
`GFileMonitor` fires `CHANGES_DONE_HINT` / `CREATED` / `RENAMED` → 150 ms debounce → `reload_with_cursor`. Reload is skipped when the buffer is dirty (preserves edits) or when `fnv1a_hash(on_disk) == original_hash` (self-save echo). Cursor line is captured before the reload and restored after.

## Build system

`Makefile` uses `gcc -std=c17 -Wall -Wextra -O2 -MMD -MP`. The `-MMD -MP` flags emit per-object `.d` dependency files which are `-include`d so header edits correctly invalidate dependent objects (previously a stale-build hazard).

## Persistence paths

| File | Purpose |
|------|---------|
| `~/.config/notes-md/settings.conf` | Editor settings |
| `~/.config/notes-md/connections.conf` | SFTP profiles |
| `$XDG_RUNTIME_DIR/notes-ssh-*/ctl` | SSH ControlMaster socket (per session) |

## External assets

`data/webview/` ships next to the executable (`../data/webview` relative to `build/notes-md`):

- `preview.html`, `preview.css`, `preview.js` — markdown renderer UI (`preview.js` bootstraps the math engine picked via `?engine=` query)
- `katex/` — vendored KaTeX (`katex.min.js`, `katex.min.css`, `fonts/*.woff2`) — synchronous math renderer (default)
- `mathjax/` — vendored MathJax (tex-chtml bundle + CHTML fonts + TeX extensions) — optional, richer LaTeX, async typeset. Includes a no-op stub at `output/svg/fonts/tex.js` to unblock a loader-path resolution the bundle issues under `POLICY_NEVER`; see changelog.
- `vendor/` — marked, highlight.js, and Mermaid for fenced diagram blocks
