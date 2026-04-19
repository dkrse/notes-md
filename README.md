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
- SSH/SFTP remote file browsing and editing (via OpenSSH ControlMaster)
- Ctrl + mouse wheel zoom in editor and preview
- Preview alignment: centered or full-width (left-aligned)
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

## Documentation

- [Architecture](docs/architecture.md)
- [Changelog](docs/changelog.md)

## Author

krse

## License

MIT — see [LICENSE](LICENSE).
