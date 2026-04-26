# Changelog

## 2026-04-26

### Fixed
- **In-document anchor links in preview.** `marked` doesn't emit heading `id` attributes by default, so `[text](#nadpis)` had nothing to scroll to. Added a custom `renderer.heading` in `data/webview/preview.js` that slugifies heading text (lowercase, unicode-letters preserved so diacritics like `č`/`š`/`ť` survive, punctuation stripped, spaces → `-`, per-render collision counter for duplicate headings). Also added a global click handler that intercepts `<a href="#…">` clicks and calls `scrollIntoView` — needed because the preview URL carries `?engine=…` query params, so default anchor navigation would rewrite the URL instead of jumping.

### Known limitation
- **PDF export does not preserve internal hyperlinks.** WebKitGTK's print path goes through cairo's PDF backend without emitting `CAIRO_TAG_LINK` annotations for `<a href="#…">`, so anchor links render as plain blue text in the exported PDF. This is a WebKitGTK limitation (Blink/Chromium emits them; WebKitGTK does not) shared by other GTK markdown editors (Apostrophe, Marker). Workarounds would require either post-processing the PDF with poppler+cairo to re-add link annotations, or shelling out to headless Chromium / WeasyPrint for the export. Not implemented.

## 2026-04-21 (2nd pass)

### Fixed
- **MathJax output regression (0.2.1 → fix).** The 0.2.1 refactor introduced a KaTeX path and rewrote the MathJax path to pre-process `$…$` / `$$…$$` → `\(…\)` / `\[…\]` *before* `marked.parse()`. Under CommonMark/GFM, `\[ \] \( \)` are valid ASCII-punctuation backslash-escapes: marked swallowed the backslashes, MathJax then saw plain `[…]` / `(…)` in the DOM, `typesetPromise` resolved with zero `mjx-container` elements, and formulas stayed as raw text. Restored the 0.2 technique: register `mathBlock` / `mathInline` as marked extensions so math is captured in the tokenizer *before* escape processing, and register the mermaid-aware renderer once globally via `marked.use({ renderer })` — passing a fresh renderer per call to `marked.parse(..., { renderer })` overwrote the extension renderers in newer marked versions.
- **MathJax typeset hang under `POLICY_NEVER`.** `data/webview/mathjax/output/svg/fonts/tex.js` added as an empty stub. With `disable_gpu=1` (required on nvidia/Wayland) the `tex-chtml` bundle resolves a path to `output/svg/fonts/tex.js` while initialising the `color`/`boldsymbol`/`cancel` extensions. The file is absent from the vendored tree; the HTTP fetch 404s; `MathJax.loader.load` rejects; `mathjax.retryAfter()` awaits the rejected promise and never resolves; `typesetPromise` hangs forever. The stub satisfies the fetch with a no-op so the loader proceeds — CHTML output is already registered by the bundle.

## 2026-04-21

### Added
- **External file watch** (`watch_file` setting, default on): `GFileMonitor` attached in `start_file_watch` whenever a local file is loaded. On `CHANGES_DONE_HINT` / `CREATED` / `RENAMED` the change is debounced 150 ms, then `reload_with_cursor` re-reads the file and restores the cursor line. Skipped when the buffer is dirty (preserves in-progress edits) or when the on-disk hash matches `original_hash` (filters out self-save echoes). SFTP mounts are excluded. Toggle in Settings → Editor: "Reload on File Change".
- **Preview GPU compositing toggle** (`disable_gpu` setting, default on): when set, `webkit_settings_set_hardware_acceleration_policy(…NEVER)` forces software rendering — workaround for nvidia/Wayland GBM buffer failures that otherwise leave the preview blank even though the DOM is populated. Toggle in Settings → Editor: "Disable Preview GPU" (requires app restart to apply).

### Changed
- **Math engine is now selectable** (`math_engine` setting: `katex` | `mathjax`, default `katex`).
  - **KaTeX** (vendored at `data/webview/katex/`, ~290 KB): synchronous `katex.renderToString` — `el.innerHTML = …` is the single layout point and repaint is consistent even on software-rendered WebKit. Default, recommended for the nvidia/Wayland path where MathJax CHTML left off-screen formulas unpainted until scrolled.
  - **MathJax** (vendored at `data/webview/mathjax/`, ~3.9 MB): async `typesetPromise` — richer LaTeX (auto linebreak for displays, `mhchem`, `\newcommand`, full AMS). Followed by a `display:none` → reflow → `display:''` toggle in `preview.js` as a best-effort off-screen repaint fix.
  - `preview.html` is now a minimal stub; `preview.js` bootstraps the chosen engine dynamically based on the `?engine=` query param set by `preview.c` from `settings.math_engine`. Switching engines requires an app restart.
  - Dropdown in Settings → Editor: "Math Engine" (KaTeX / MathJax).

### Fixed
- **`snprintf` self-alias UB in `notes_window_load_file`**: when the caller passes `win->settings.last_file` as `path` (the startup restore path), the line `snprintf(win->settings.last_file, n, "%s", path)` performs an overlapping copy where source and destination are the same buffer. On glibc this zeroes the buffer. Subsequent `start_file_watch(win, path)` saw an empty string and never attached a monitor. Fix: monitor binds to `win->current_file`, which is copied into a separate buffer immediately before.

---

## 2026-04-19 (3rd pass)

### Security
- **ssh.c:** `ssh_arg_is_safe()` validates host/user — rejects empty, leading `-` (option injection), and control chars / newlines. `ssh_argv_new` returns NULL on invalid input; all callers (`ssh_cat_file`, `ssh_write_file`, remote browse) check and abort.
- **preview.html CSP** tightened: `object-src 'none'`, `base-uri 'none'`, `frame-src 'none'`, default-src narrowed to `'self' file:`, img-src explicitly allows `https:` + `data:`. `unsafe-inline` / `unsafe-eval` remain (MathJax requires them).
- **PDF tmp file symlink race fixed:** `pdf_add_page_numbers` now uses `g_mkstemp` + `g_chmod(0600)` instead of predictable `.tmp` suffix.
- **Password authentication removed from SFTP dialog.** Password field, `password_entry`/`password_row` struct fields, visibility toggling, and clear/reset calls all deleted. Auth is now publickey-only (default keys or user-specified `-i <path>`); `BatchMode=yes` remains enforced. Rationale: no passwords in memory/config, stronger default, simpler code.

### Added
- **Connect-test stderr surfacing:** `ssh_connect_thread` now captures stderr and appends it to the error dialog, so users see actual SSH errors (`Permission denied (publickey)`, `Host key verification failed`, etc.) instead of just `exit 255`.
- **Search debouncing:** `on_search_changed` debounces `search_highlight_all` by 150 ms via `search_debounce_id`, cancelled in `on_destroy`. Avoids O(n²) re-highlights on every keystroke for large files.

### Build
- `Makefile` now links `poppler-glib` + `cairo` (used by PDF page-number post-processing).

---

## 2026-04-19

### Added (2nd pass)
- **Find-in-preview:** Ctrl+F while the Preview tab is active opens a find bar above the webview (GtkSearchEntry + prev/next/close) wired to `WebKitFindController` (`counted-matches` → match count label, `failed-to-find-text` → "Not found"). Enter cycles forward, Shift+Enter backward, Escape closes.
- **Preview font size setting** (`preview_font_size`, default 14pt) applied as WebKit zoom ratio (size/14). Exposed via spin button in Settings → Editor. Ctrl+scroll on the preview now updates this persistent setting instead of a session-only zoom.
- **PDF export** (`Export to PDF...` menu item, Ctrl+Shift+P) via `webkit_print_operation_*` + `GtkPageSetup`. Settings dialog gains a new **PDF** tab:
  - Top/bottom/left/right margins in mm (0–100)
  - Landscape toggle
  - Page numbers: None / "Page N" / "Page N of M" — injected as a `@media print { @page { @bottom-center { … } } }` style element into the preview document just before printing.
  - One-click "Export PDF Now..." button inside the PDF tab.
- Settings dialog is now split into tabs via `GtkStackSwitcher` (`Editor`, `PDF`).

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
