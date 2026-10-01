# File Sorter

A fast, keyboard-friendly Windows 10/11 app for sorting a messy folder one file at a time:
preview the file, then send it to one of up to five destination folders, delete it, or skip it.

Native C++ / Win32 with no runtime dependencies: a single `FileSorter.exe`.

## How it was built

File Sorter was built with Claude Code. I came up with the idea and design, defined the features and UX, directed the implementation, and tested it on Windows 10/11 hardware. Any future development of this project will be with AI assistance. As of now, there are no plans for further development.

## Using it

| Area                          | What it does                                                                                                                                                                                                           |
| ----------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Folder to sort** (top left) | _Browse…_ (Ctrl+O), type or paste a path and press Enter, or drop a folder onto the window. Shows file count, total size and how many files are hidden/system.                                                         |
| **Files** (bottom left)       | Every file in the folder, **including hidden and system files** (shown dimmed). Name, Type and Size are always shown; _Columns_ adds Date modified/created/accessed, Extension and Attributes. Click a header to sort. |
| **Preview** (top right)       | Previews the selected file. The first file is selected automatically, and after each move/delete the next file is selected. _Open_ (or Enter / double-click) opens it in its default app.                              |
| **Sort to** (bottom right)    | Five destination buttons. Click an empty one to name it and pick its folder; click the pencil (or right-click) to edit or remove it. Clicking a configured button **moves** the file there, with no copy left behind.  |

### Keyboard

| Key         | Action                                    |
| ----------- | ----------------------------------------- |
| `1` – `5`   | Move the selected file to destination 1–5 |
| `Del`       | Delete (to the Recycle Bin)               |
| `Shift+Del` | Delete permanently (always asks first)    |
| `Space`     | Skip to the next file                     |
| `Ctrl+Z`    | Undo the last move                        |
| `↑` / `↓`   | Previous / next file                      |
| `Enter`     | Open the file in its default app          |
| `Ctrl+O`    | Choose a folder                           |
| `F5`        | Refresh                                   |
| `Ctrl+T`    | Toggle light / dark mode                  |

### Safety details

- Moves never overwrite. If the destination already has a file with that name, the moved file becomes `name (2).ext`, and the status bar says so.
- Moves across drives run in the background with progress, and the original is removed only after the copy succeeds.
- _Delete_ sends files to the Recycle Bin. _Ask before deleting_ is on by default.
- _Undo_ (Ctrl+Z) moves sorted files back, up to the last 100 moves.

### What can be previewed

1. **Images** via Windows Imaging Component: JPEG, PNG, GIF, BMP, TIFF, ICO, JPEG XR, DDS, plus HEIC, WebP, AVIF and camera RAW when their Windows codecs are installed. EXIF rotation is applied, and transparency is shown on a checkerboard.
2. **Audio & video**: play, pause and seek in place with Media Foundation (MP4, MOV, WMV, AVI, MKV, WebM, MP3, WAV, FLAC, M4A, …). Album art or a poster frame is shown when available.
3. **Text & source code**: UTF-8, UTF-16 and ANSI, in a monospace viewer that follows the theme.
4. **Anything with a Windows preview handler**, the same previewers File Explorer uses: PDF, Word, Excel, PowerPoint, Outlook mail, HTML, SVG, RTF, fonts, and anything installed by other apps (e.g. PowerToys).
5. **Archives**: ZIP-based files (zip, jar, apk, epub, nupkg, …) are listed directly. 7z, RAR and TAR are listed on Windows 11 versions that support them.
6. **Thumbnails** from any installed thumbnail provider.
7. **Hex view** of the first 4 KB for everything else.

## Building

### Visual Studio 2022/2026 (recommended on Windows)

Install the **Desktop development with C++** workload (it includes CMake), then either:

- _File → Open → Folder…_ on this directory, pick the `x64-Release` configuration, and build; or
- from a _Developer PowerShell for VS_:

    ```powershell
    cmake -S . -B build -A x64          # uses the newest Visual Studio found
    cmake --build build --config Release
    # -> build\Release\FileSorter.exe
    ```

The CRT is linked statically, so the resulting `.exe` runs on any Windows 10/11 PC without extra installs.

### MinGW-w64 / llvm-mingw (e.g. cross-compiling from Linux or WSL)

```bash
CXX=/path/to/x86_64-w64-mingw32-clang++ ./build-mingw.sh   # -> dist/FileSorter.exe
```

A CMake toolchain build also works: `cmake -S . -B build -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_CXX_COMPILER=... -DCMAKE_RC_COMPILER=...`.

## Project layout

```
src/
  main.cpp               entry point (COM, Media Foundation, command-line folder)
  App.*                  main window: layout, splitters, commands, move/delete/undo workflow
  FileList.*             folder scan + virtual list view (columns, sorting, hidden files)
  PreviewPane.*          all preview renderers (WIC, Media Foundation, preview handlers, text, zip, hex)
  DestinationDialog.*    "Set up destination" dialog
  Button.*               custom-drawn buttons, toggles and destination tiles
  Theme.*                light/dark palettes, fonts, dark title bar / scrollbars / menus
  Settings.*             %APPDATA%\FileSorter\settings.ini
res/                     icon, manifest (per-monitor DPI v2, common controls v6), version info
```

Settings (theme, destinations, window layout, columns) are saved to `%APPDATA%\FileSorter\settings.ini`.

## License

[MIT](LICENSE) © 2026 Justin Whitfield
