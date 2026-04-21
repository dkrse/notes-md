# notes-md

GTK 4 / libadwaita markdown editor written in C with live WebKit preview, GtkSourceView syntax highlighting, and SSH/SFTP remote file editing.

## Features

- Live markdown preview (WebKit) with selectable themes
- MathJax math rendering (`$…$`, `$$…$$`, `\(…\)`, `\[…\]`)
- Mermaid diagram rendering in fenced `mermaid` code blocks
- Syntax highlighting via GtkSourceView 5 (editor) and highlight.js (preview)
- Configurable fonts, line spacing, font intensity, themes (light/dark/solarized/monokai/nord/dracula/tokyo-night/catppuccin…)
- Line numbers, current-line highlight, word-wrap toggle
- Find/replace with scrollbar markers
- SSH/SFTP remote file browsing and editing (via OpenSSH ControlMaster, **SSH-key auth only**)
- Ctrl + mouse wheel zoom in editor and preview (persisted per-view)
- Preview alignment: centered or full-width (left-aligned)
- Preview font size setting (independent from editor)
- Find-in-preview (Ctrl+F in preview mode) via WebKit find controller
- External file watch — auto-reload when the file changes on disk (skipped while you have unsaved edits; toggleable in Settings)
- Preview GPU compositing toggle — software-renderer fallback for broken GPU drivers (nvidia/Wayland)
- PDF export with configurable margins, landscape/portrait, and page numbers
- Persistent per-user settings and SFTP connection profiles

## Build

Requires: `libadwaita-1`, `gtksourceview-5`, `webkitgtk-6.0`, `gcc`, `make`, `pkg-config`.

```sh
make
./build/notes-md
```

## Configuration

- `~/.config/notes-md/settings.conf` — editor and preview settings
- `~/.config/notes-md/connections.conf` — SFTP connection profiles

## SSH setup

notes-md uses SSH key authentication (passwords are intentionally not supported — `BatchMode=yes` is always enforced). Set up a key if you don't have one:

```sh
ssh-keygen -t ed25519
ssh-copy-id -p 22 user@host
```

In the SFTP connection dialog, either leave "Use SSH Key" unchecked (tries default keys `~/.ssh/id_*`) or check it and point to a specific key file.

## Documentation

- [Architecture](docs/architecture.md)
- [Diagrams](docs/diagrams.md)
- [Changelog](docs/changelog.md)

## Author

krse

## License

MIT — see [LICENSE](LICENSE).
