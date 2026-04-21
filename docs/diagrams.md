# Diagrams

Mermaid diagrams of the notes-md architecture and key runtime flows.

## Module dependency graph

```mermaid
graph LR
    main[main.c] --> window[window.c]
    window --> actions[actions.c]
    window --> preview[preview.c]
    window --> settings[settings.c]
    window --> ssh[ssh.c]
    actions --> preview
    actions --> settings
    actions --> ssh
    preview -.->|loads| webview[(data/webview/<br/>preview.html)]
    preview -.->|poppler post-process| PDF[(output.pdf)]
    ssh -.->|spawns| openssh([OpenSSH CLI])
    settings -.->|reads/writes| cfg[(~/.config/notes-md/)]
```

## Local file open

```mermaid
sequenceDiagram
    participant U as User
    participant A as actions.c
    participant W as window.c
    participant FS as filesystem
    U->>A: File → Open
    A->>A: GtkFileDialog
    A->>W: notes_window_load_file(path)
    W->>FS: fopen/fread (≤ 5 MB, ferror check)
    FS-->>W: bytes
    W->>W: binary detect (NUL scan first 8 KB)
    W->>W: GtkTextBuffer.set_text
    W->>W: fnv1a_hash → original_hash
    W->>W: set title, status bar
```

## Remote (SSH) file open

```mermaid
sequenceDiagram
    participant U as User
    participant Dlg as SFTP dialog<br/>(actions.c)
    participant W as window.c
    participant S as ssh.c
    participant R as remote host
    U->>Dlg: Connect (host/user/key)
    Dlg->>Dlg: ssh_arg_is_safe(host, user)
    Dlg->>R: ssh ... -- echo ok (connect test)
    R-->>Dlg: exit 0
    Dlg->>W: notes_window_ssh_connect
    W->>S: ssh_ctl_start (ControlMaster=yes, -fN)
    S->>R: open ControlMaster socket
    U->>Dlg: pick remote file
    Dlg->>S: ssh_cat_file(remote_path)
    S->>R: ssh ... -- cat <path>  (via ctl socket)
    R-->>S: file bytes (≤ max_file_size)
    S-->>W: contents, len
    W->>W: same load path as local open
```

## PDF export with page numbers

```mermaid
sequenceDiagram
    participant U as User
    participant A as actions.c
    participant P as preview.c
    participant WK as WebKit
    participant Pop as poppler/cairo
    U->>A: Export to PDF
    A->>A: GtkFileDialog.save
    A->>P: preview_export_pdf(path)
    P->>P: build GtkPageSetup<br/>(margins + landscape)
    P->>WK: webkit_print_operation_print<br/>(OUTPUT_URI, Print to File)
    WK-->>P: writes initial PDF
    alt page_numbers != "none"
        P->>P: g_timeout_add(500, pdf_post_process_cb)
        loop until file size > 0 (≤ 20 s)
            P->>P: stat(path)
        end
        P->>Pop: poppler_document_new_from_file
        P->>Pop: g_mkstemp (tmp PDF, 0600)
        loop each page
            Pop->>Pop: render_for_printing → cairo
            Pop->>Pop: draw "N / M" at bottom
        end
        P->>P: g_rename(tmp → original)
    end
```

## Preview update pipeline

```mermaid
sequenceDiagram
    participant B as GtkTextBuffer
    participant P as preview.c
    participant WK as WebKit
    participant JS as preview.js
    B->>P: changed (signal)
    P->>P: preview_queue_update<br/>(250 ms debounce)
    Note over P: update_timeout_cb fires
    P->>P: get_text + json_escape
    P->>WK: evaluate_javascript:<br/>nmdRender(text)
    WK->>JS: render()
    JS->>JS: marked.parse → innerHTML
    JS->>JS: mermaid.render each block
    JS->>JS: MathJax typeset
    JS->>JS: display:none → reflow → display:''<br/>(force repaint for software renderer)
```

## External file reload (watch_file)

```mermaid
sequenceDiagram
    participant FS as filesystem
    participant M as GFileMonitor
    participant W as window.c
    participant B as GtkTextBuffer
    participant P as preview.c
    Note over M: attached in start_file_watch<br/>(skipped for SFTP mounts)
    FS-->>M: inotify event
    M->>W: changed signal<br/>(CHANGES_DONE_HINT / CREATED / RENAMED)
    W->>W: 150 ms debounce
    Note over W: reload_with_cursor
    alt buffer dirty
        W-->>W: skip (preserve user edits)
    else hash matches original_hash
        W-->>W: skip (self-save echo)
    else
        W->>W: save cursor line
        W->>W: notes_window_load_file (current_file)
        W->>B: set_text (buffer "changed")
        B->>P: on_buffer_changed_preview
        P->>P: preview_queue_update
        W->>W: restore cursor line
    end
```

## Find-in-preview

```mermaid
sequenceDiagram
    participant U as User
    participant P as preview.c
    participant FC as WebKitFindController
    U->>P: Ctrl+F (preview visible)
    P->>P: preview_find_show (reveal bar)
    U->>P: type query
    P->>FC: webkit_find_controller_search<br/>(WRAP_AROUND | CASE_INSENSITIVE)
    FC-->>P: counted-matches(N) → label
    FC-->>P: failed-to-find-text → "Not found"
    U->>P: Enter / ↑ / ↓
    P->>FC: search_next / search_previous
    U->>P: Esc
    P->>FC: search_finish
```

## Settings persistence

```mermaid
stateDiagram-v2
    [*] --> Defaults: settings_load (no file)
    [*] --> Parsed: fopen + key=value
    Parsed --> Memory
    Defaults --> Memory
    Memory --> Dirty: user change
    Dirty --> Atomic: settings_save
    Atomic --> Atomic: g_mkstemp(.XXXXXX)
    Atomic --> Atomic: fprintf + fflush + fclose
    Atomic --> Committed: g_rename
    Atomic --> Discarded: error → g_remove
    Committed --> Memory
```
