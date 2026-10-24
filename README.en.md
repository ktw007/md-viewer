# md-viewer

A simple local Markdown document viewer. 一个简单的本地 Markdown 文档查看器。

[简体中文](README.md) | [English](README.en.md)

A native Windows desktop app: a C++20 / Win32 window shell that renders pages with WebView2. It does one thing: lay out local `.md` files in a form that is comfortable to read for long stretches.

## Features

- **Reading-first typography**: serif fonts and generous line spacing. Source Serif 4 and subsetted Source Han Serif SC (思源宋体, 7500+ common characters and symbols) are bundled as compact WOFF2, so nothing depends on system fonts or the network.
- **Syntax highlighting**: bundled offline Prism.js syntax highlighting for C/C++, C#, Java, Python, Rust, Go, JavaScript, TypeScript, Bash, JSON, YAML, SQL, HTML/CSS, Markdown, and more, seamlessly adapted to the Paper, Ink, and Mist themes.
- **Table of contents**: a sidebar outline is generated automatically; click to jump.
- **Change notice**: the current file is watched. When you save it in your editor, the viewer shows "File updated" (「文件已更新」); click "Refresh" (「刷新」) or press `F5` to reload. If the file is moved or deleted, it shows "File moved or deleted" (「文件已移动或删除」).
- **Encoding detection**: tried in this order: UTF-8 (with or without BOM), UTF-16 LE with BOM, UTF-16 BE with BOM, GB18030, then the system ANSI code page. Files larger than 32 MB or containing NUL bytes are refused.
- **Relative paths work**: relative links and images in a document resolve against the file's own location.
- **Single instance**: opening another file while the viewer is running hands it to the existing window instead of starting a second one.
- **Remembers state**: the next launch restores window size and position, maximized state, zoom, each file's scroll position, plus theme, font size, content width, font, line height and sidebar. Recent files, highlights and notes are kept too.
- **Back and forward**: after following a local Markdown link, `Alt` + `←` / `Alt` + `→` move between documents opened in this session and restore each one's scroll position.
- **Lightweight**: a native program with no Electron or Node runtime. Markdown is parsed with [md4c](https://github.com/mity/md4c).

## Requirements

- Windows 10 or Windows 11, x64 only (the build links only the x64 WebView2Loader)
- [WebView2 Runtime](https://developer.microsoft.com/microsoft-edge/webview2/). It ships with Windows 11; on Windows 10 you may need to install the Evergreen Runtime.

## Quick start

Download [md-viewer-windows-x64.zip](https://github.com/ktw007/md-viewer/releases/latest/download/md-viewer-windows-x64.zip) from [Releases](https://github.com/ktw007/md-viewer/releases/latest), extract it, and run `md-viewer.exe`. Keep the `assets` folder next to the executable.

You can also [build from source](#building-from-source).

Pass a file path to open it:

```powershell
md-viewer.exe path\to\document.md
```

You can also drag `.md`, `.markdown`, `.mdown` or `.mkd` files onto the window, or use Open File. The app does not register itself as the default handler for `.md`; if you want that, pick `md-viewer.exe` yourself via Windows "Open with".

## Usage

### Command line and single instance

```powershell
md-viewer.exe <file>
```

If an md-viewer window is already running, the new invocation sends the path to it and exits; the existing window opens the file.

### Keyboard shortcuts

| Key | Action |
|---|---|
| `F5` | Reload the current file |
| `Ctrl` + `+` / `Ctrl` + `-` | Zoom in / out |
| `Ctrl` + `0` | Reset zoom to 100% |
| `Ctrl` + `F` | Find |
| `Ctrl` + `K` | Search headings and body text |
| `Ctrl` + `\` or `[` | Show / hide the sidebar outline |
| `Ctrl` + `P` | Print |
| `J` / `K` | Next / previous section |
| `T` | Toggle theme |
| `G` `G` (press twice) | Back to top |
| `?` | Show the shortcut sheet |
| `Alt` + `←` / `Alt` + `→` | Back / forward between documents opened in this session |

History is kept in memory only: following a local Markdown link from the current document adds an entry; `Alt` + `←` returns to the previous document and restores its scroll position, `Alt` + `→` goes forward. Opening a document from the command line, by drag-and-drop or with Open File clears the history, and it is not restored on the next launch.

### Where settings are stored

- **Native settings**: `%APPDATA%\MdViewer\session.json` (i.e. `C:\Users\<username>\AppData\Roaming\MdViewer\session.json`). It stores window position and size (only used if width ≥ 480 and height ≥ 320), maximized state, page zoom (0.5–3.0), and each file's reading position (keyed by normalized absolute path; the value is a 0–1 scroll ratio, not a heading anchor). Delete it to reset the window and zoom to defaults.
- **Reading preferences and page data**: stored in the page's localStorage under the WebView2 user data folder `%LOCALAPPDATA%\MdViewer\WebView2`:
  - `mdv-reading`: theme (Paper 纸, Ink 墨, Mist gray 雾灰, or follow system), font size (15–21px), content width, serif / sans-serif, line height, sidebar open state and width (200–420px);
  - `mdv-recent`: the last 8 recent files;
  - `mdv-marks:` / `mdv-notes:`: per-file highlights and notes.
- **Page cache**: `%LOCALAPPDATA%\MdViewer\cache`.

## Building from source

### Prerequisites

- Windows 10/11
- Visual Studio 2022 (or Build Tools) with the "Desktop development with C++" workload; the compiler must support C++20
- [CMake](https://cmake.org/) 3.24 or newer
- [Ninja](https://ninja-build.org/)

### 1. Get the code

```powershell
git clone https://github.com/ktw007/md-viewer.git
cd md-viewer
```

### 2. Prepare third-party dependencies

md4c is bundled (`third_party/md4c`). The WebView2 SDK and WIL are **not committed**. You can download and install them automatically using the helper script:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup-deps.ps1
```

The script fetches verified stable NuGet packages and extracts them to `third_party/webview2` and `third_party/wil`.

Alternatively, you can place them manually:

| Dependency | Location | Source |
|---|---|---|
| md4c | `third_party/md4c` | bundled |
| WebView2 SDK | `third_party/webview2` | NuGet package [Microsoft.Web.WebView2](https://www.nuget.org/packages/Microsoft.Web.WebView2) |
| WIL | `third_party/wil` | NuGet package [Microsoft.Windows.ImplementationLibrary](https://www.nuget.org/packages/Microsoft.Windows.ImplementationLibrary), or [microsoft/wil](https://github.com/microsoft/wil) |

Check that these paths exist after extraction:
- `third_party/webview2/build/native/include` (headers such as `WebView2.h`)
- `third_party/webview2/build/native/x64/WebView2LoaderStatic.lib` (static link, x64 only)
- `third_party/wil/include` (e.g. `wil/com.h`)

### 3. Build

Run in the "x64 Native Tools Command Prompt for VS" so that `cl` is on PATH:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The output is `build\md-viewer.exe`. The build copies the whole `page/` directory next to the executable as `assets/`, which it needs at runtime.

### 4. Run the tests

Run the test suite via CTest (includes Markdown render tests and platform encoding tests):

```powershell
ctest --test-dir build --output-on-failure
```

You can also run the test executables directly:

```powershell
build\md-render-test.exe
build\md-platform-test.exe
```

## Project layout

```text
md-viewer/
├─ src/
│  ├─ document/   Markdown rendering, escaping, link/image path rewriting, HTML page assembly
│  ├─ app/        window, file loading, message handling
│  ├─ webview/    WebView2 host
│  └─ platform/   encoding detection, settings persistence, file watching, paths
├─ page/          page layer: viewer.css, viewer.js
├─ page/fonts/    bundled fonts (Source Serif 4, Source Han Serif SC) and OFL.txt
├─ resources/     icon, app.manifest, app.rc
├─ scripts/       dependency setup and helper scripts
├─ tests/         render tests, platform encoding tests, and fixture documents
├─ third_party/   md4c (bundled); webview2 and wil (fetched automatically via script)
└─ CMakeLists.txt
```

Rough flow: the file path comes from the command line → `platform` reads the file and detects its encoding → `document` parses it to HTML with md4c, escapes it, rewrites paths and assembles a full page → `webview` loads it in WebView2, where `viewer.js` handles the outline, zoom and other interaction → `platform` watches the file and the page prompts you to refresh when it changes.

## Known limitations

- Windows 10 / 11 x64 only, and the WebView2 Runtime must be installed.
- Reading only: the Markdown source cannot be edited. Highlights and notes stay in local localStorage and are not written back to the file.
- Markdown uses md4c's GitHub dialect: tables, strikethrough, task lists, permissive autolinks, and CommonMark headings, lists, quotes, fenced code blocks and indented code blocks. LaTeX math and wiki links are not rendered.
- Raw HTML in the source is not executed as a page. Tags are escaped and shown as text, so `<script>` appears as text.

## Contributing

Issues and pull requests are welcome. Please make sure `md-render-test` passes before submitting.

Sources are UTF-8. C++ is written as C++20 with 4-space indentation. The repository has no separate formatter config or commit-message convention.

## License

The project code is released under the MIT License; see [LICENSE](LICENSE). The bundled fonts Source Serif 4 and Source Han Serif SC (思源宋体) are licensed under the SIL Open Font License 1.1 (see [page/fonts/OFL.txt](page/fonts/OFL.txt)). md4c is under the MIT License; see [third_party/md4c/LICENSE.md](third_party/md4c/LICENSE.md). WIL is under the MIT License.

The WebView2 SDK is not included in this repository; if a future installer ships WebView2Loader, that part is covered by Microsoft's license terms, not MIT.

## Acknowledgements

- [md4c](https://github.com/mity/md4c): Markdown parsing
- [WebView2](https://developer.microsoft.com/microsoft-edge/webview2/) and [WIL](https://github.com/microsoft/wil): Microsoft
- [Source Serif 4](https://github.com/adobe-fonts/source-serif) and [Source Han Serif](https://github.com/adobe-fonts/source-han-serif): bundled fonts