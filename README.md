# md-viewer

一个简单的本地 Markdown 文档查看器。A simple local Markdown document viewer.

[简体中文](README.md) | [English](README.en.md)

Windows 原生桌面应用,用 C++20 和 Win32 做窗口外壳,用 WebView2 渲染页面。只做一件事:把本地的 `.md` 文件排成适合长时间阅读的样子。

## 特点

- **阅读优先的排版**:衬线字体、宽松行距,内置 Source Serif 4 和思源宋体,不依赖系统字体,无需联网加载。
- **章节目录**:左侧自动生成目录,点击跳转。
- **文件变化提示**:监视当前文件,你在编辑器里保存后,查看器会提示「文件已更新」,点击「刷新」或按 `F5` 重新加载;文件被移动或删除时提示「文件已移动或删除」。
- **编码自动识别**:按以下顺序识别:UTF-8(含 BOM)、带 BOM 的 UTF-16 LE、带 BOM 的 UTF-16 BE、GB18030、系统 ANSI 代码页。超过 32 MB 或含 NUL 字节的文件会被拒绝打开。
- **相对路径可用**:文档里的相对链接和图片会按文件所在位置正确解析。
- **单实例**:已经打开了查看器时,再次打开文件会把它交给正在运行的窗口,而不是再开一个。
- **记住状态**:下次启动会恢复窗口大小与位置、是否最大化、缩放、每份文件的滚动位置,以及主题、字号、版心、字体、行距和侧栏。最近文件、高亮和批注也会保留。
- **前进后退**:点击文档里的本地 Markdown 链接后,可用 `Alt` + `←` / `Alt` + `→` 在本次打开过的文档之间切换,并恢复各自的滚动位置。
- **轻量**:原生程序,没有 Electron 或 Node 运行时,Markdown 解析使用 [md4c](https://github.com/mity/md4c)。

## 系统要求

- Windows 10 或 Windows 11,仅 x64(构建只链接 x64 版 WebView2Loader)
- [WebView2 Runtime](https://developer.microsoft.com/microsoft-edge/webview2/)。Windows 11 已自带,Windows 10 如果没有需要自行安装 Evergreen Runtime。

## 快速开始

到 [Releases](https://github.com/ktw007/md-viewer/releases/tag/v1.0.0) 下载 [md-viewer-windows-x64.zip](https://github.com/ktw007/md-viewer/releases/download/v1.0.0/md-viewer-windows-x64.zip)，解压后运行 `md-viewer.exe`。`assets` 目录要留在 exe 旁边。

也可以按下文[从源码构建](#从源码构建)。

把文件路径作为参数传给程序即可打开:

```powershell
md-viewer.exe path\to\document.md
```

也可以把 `.md`、`.markdown`、`.mdown`、`.mkd` 文件拖放到窗口中打开,或使用「打开文件」。程序不会把自己注册为 `.md` 的默认打开方式;如有需要,可在 Windows 的「打开方式」中自行选择 `md-viewer.exe`。

## 使用说明

### 命令行与单实例

```powershell
md-viewer.exe <文件路径>
```

如果已有一个 md-viewer 窗口在运行,新的调用会把文件路径发给它并退出,由已有窗口打开该文件。

### 快捷键

| 按键 | 作用 |
|---|---|
| `F5` | 重新加载当前文件 |
| `Ctrl` + `+` / `Ctrl` + `-` | 放大 / 缩小 |
| `Ctrl` + `0` | 缩放重置为 100% |
| `Ctrl` + `F` | 查找 |
| `Ctrl` + `K` | 搜索标题与正文 |
| `Ctrl` + `\` 或 `[` | 显示 / 隐藏侧栏目录 |
| `Ctrl` + `P` | 打印 |
| `J` / `K` | 下一节 / 上一节 |
| `T` | 切换主题 |
| `G` `G`(连按两次) | 回到顶部 |
| `?` | 显示快捷键表 |
| `Alt` + `←` / `Alt` + `→` | 在本次打开过的文档之间后退 / 前进 |

历史记录只保存在内存中:从当前文档点击本地 Markdown 链接会记入一条;`Alt` + `←` 回到上一个文档并恢复其滚动位置,`Alt` + `→` 前进。通过命令行、拖放或「打开文件」打开文档会清空历史,下次启动也不会恢复。

### 设置保存位置

- **原生设置**:`%APPDATA%\MdViewer\session.json`(即 `C:\Users\<用户名>\AppData\Roaming\MdViewer\session.json`)。保存窗口位置与大小(宽 ≥ 480、高 ≥ 320 时才有效)、是否最大化、页面缩放(0.5–3.0),以及每份文件的阅读位置(以规范化后的绝对路径为键,值为 0–1 的滚动比例,不是标题锚点)。删除该文件即可把窗口和缩放恢复为默认。
- **阅读偏好与页面数据**:保存在 WebView2 用户数据目录 `%LOCALAPPDATA%\MdViewer\WebView2` 下的页面 localStorage 中:
  - `mdv-reading`:主题(纸、墨、雾灰、跟随系统)、字号(15–21px)、版心宽度、衬线 / 无衬线、行距、侧栏是否展开及宽度(200–420px);
  - `mdv-recent`:最近打开的 8 个文件;
  - `mdv-marks:` / `mdv-notes:`:每份文件的高亮与批注。
- **页面缓存**:`%LOCALAPPDATA%\MdViewer\cache`。

## 从源码构建

### 前置条件

- Windows 10/11
- Visual Studio 2022(或 Build Tools),安装"使用 C++ 的桌面开发"工作负载,编译器需支持 C++20
- [CMake](https://cmake.org/) 3.24 或更高
- [Ninja](https://ninja-build.org/)

### 1. 获取代码

```powershell
git clone https://github.com/ktw007/md-viewer.git
cd md-viewer
```

### 2. 准备第三方依赖

仓库自带 md4c(`third_party/md4c`)。WebView2 SDK 和 WIL **没有提交进仓库**,可以直接运行项目提供的脚本一键下载安装:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup-deps.ps1
```

脚本会自动从 NuGet 获取经过兼容性验证的稳定版本解压到 `third_party/webview2` 和 `third_party/wil`。

如需手动下载,可参考下表:

| 依赖 | 放置位置 | 来源 |
|---|---|---|
| md4c | `third_party/md4c` | 已自带 |
| WebView2 SDK | `third_party/webview2` | NuGet 包 [Microsoft.Web.WebView2](https://www.nuget.org/packages/Microsoft.Web.WebView2) |
| WIL | `third_party/wil` | NuGet 包 [Microsoft.Windows.ImplementationLibrary](https://www.nuget.org/packages/Microsoft.Windows.ImplementationLibrary),或 [microsoft/wil](https://github.com/microsoft/wil) |

解压后请确认以下路径存在:
- `third_party/webview2/build/native/include`(头文件,如 `WebView2.h`)
- `third_party/webview2/build/native/x64/WebView2LoaderStatic.lib`(静态链接,仅 x64)
- `third_party/wil/include`(如 `wil/com.h`)

### 3. 构建

在"x64 Native Tools Command Prompt for VS"中执行(这样 `cl` 才在 PATH 里):

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

构建产物为 `build\md-viewer.exe`。构建时会把整个 `page/` 目录复制到可执行文件旁的 `assets/`,运行时需要它。

### 4. 运行测试

使用 CTest 运行自动化测试套件(包括 Markdown 渲染测试与平台编码测试):

```powershell
ctest --test-dir build --output-on-failure
```

也可以直接执行对应的独立测试二进制:

```powershell
build\md-render-test.exe
build\md-platform-test.exe
```

## 项目结构

```text
md-viewer/
├─ src/
│  ├─ document/   Markdown 渲染、转义、链接和图片路径改写、组装 HTML 页面
│  ├─ app/        窗口、文件加载、消息处理
│  ├─ webview/    WebView2 宿主
│  └─ platform/   编码识别、设置保存、文件变化监视、路径处理
├─ page/          页面层:viewer.css、viewer.js
├─ page/fonts/    内置字体(Source Serif 4、思源宋体)及 OFL.txt
├─ resources/     图标、app.manifest、app.rc
├─ scripts/       依赖自动获取与构建辅助脚本
├─ tests/         渲染测试、平台编码测试及 fixtures 示例文档
├─ third_party/   md4c(自带),webview2 与 wil(可通过脚本自动拉取)
└─ CMakeLists.txt
```

大致流程:启动时从命令行取得文件路径 → `platform` 读取文件并识别编码 → `document` 用 md4c 解析为 HTML,做转义和路径改写,组装成完整页面 → `webview` 在 WebView2 中加载,页面里的 `viewer.js` 负责目录、缩放等交互 → `platform` 监视文件变化,页面提示用户刷新。

## 已知限制

- 仅支持 Windows 10 / 11 x64，并且需要已安装 WebView2 Runtime。
- 只能阅读，不能改 Markdown 原文。高亮和批注记在本机的 localStorage 里，不会写回文件。
- Markdown 使用 md4c 的 GitHub 方言：表格、删除线、任务列表、宽松的自动链接，以及 CommonMark 的标题、列表、引用、围栏代码块和缩进代码块。不渲染 LaTeX 公式和 Wiki 链接。
- 原文中的 HTML 不会当作页面执行。标签会被转义成文字显示，例如 `<script>` 只显示为文本。

## 参与贡献

欢迎提 issue 和 PR。提交前请确认 `md-render-test` 通过。

源码使用 UTF-8。C++ 按 C++20 编写，缩进为 4 个空格。仓库里没有单独的格式化配置或提交说明约定。

## 许可证

本项目代码以 MIT 许可证发布,详见 [LICENSE](LICENSE)。内置字体 Source Serif 4 与思源宋体采用 SIL Open Font License 1.1(见 [page/fonts/OFL.txt](page/fonts/OFL.txt))。md4c 采用 MIT 许可证,见 [third_party/md4c/LICENSE.md](third_party/md4c/LICENSE.md)。WIL 采用 MIT 许可证。

WebView2 SDK 不包含在本仓库中;如果将来的安装包附带 WebView2Loader,该部分遵循微软的许可条款,而非 MIT。

## 致谢

- [md4c](https://github.com/mity/md4c):Markdown 解析
- [WebView2](https://developer.microsoft.com/microsoft-edge/webview2/) 与 [WIL](https://github.com/microsoft/wil):微软
- [Source Serif 4](https://github.com/adobe-fonts/source-serif) 与 [思源宋体](https://github.com/adobe-fonts/source-han-serif):内置字体