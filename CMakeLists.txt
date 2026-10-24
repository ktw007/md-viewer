cmake_minimum_required(VERSION 3.24)
project(md-viewer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/utf-8 /permissive- /Zc:__cplusplus)
endif()

set(WEBVIEW2_DIR ${CMAKE_SOURCE_DIR}/third_party/webview2)
set(WIL_DIR ${CMAKE_SOURCE_DIR}/third_party/wil)

add_library(md4c STATIC
  third_party/md4c/md4c.c
  third_party/md4c/md4c-html.c
  third_party/md4c/entity.c
)
target_include_directories(md4c PUBLIC third_party/md4c)
if(MSVC)
  target_compile_options(md4c PRIVATE /W0)
  target_compile_definitions(md4c PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(NOT DEFINED MDV_ASSET_VERSION)
  set(MDV_ASSET_VERSION "7")
endif()

add_library(mdv_document STATIC
  src/document/escape.cpp
  src/document/render.cpp
  src/document/rewrite.cpp
  src/document/compose.cpp
)
target_link_libraries(mdv_document PUBLIC md4c)
target_include_directories(mdv_document PUBLIC src)
target_compile_definitions(mdv_document PUBLIC
  MDV_ASSET_VERSION="${MDV_ASSET_VERSION}"
)
if(MSVC)
  target_compile_options(mdv_document PRIVATE /W4)
endif()

file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}/preview" MDV_PREVIEW_DIR)
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/page" MDV_PAGE_DIR)
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/tests/fixtures" MDV_FIXTURE_DIR)

add_executable(md-render-test tests/render_test.cpp)
target_link_libraries(md-render-test PRIVATE mdv_document)
target_compile_definitions(md-render-test PRIVATE
  MDV_PREVIEW_DIR="${MDV_PREVIEW_DIR}"
  MDV_PAGE_DIR="${MDV_PAGE_DIR}"
  MDV_FIXTURE_DIR="${MDV_FIXTURE_DIR}"
)
if(MSVC)
  target_compile_options(md-render-test PRIVATE /W4)
endif()

enable_testing()
add_test(NAME RenderTest COMMAND md-render-test)

add_executable(md-platform-test
  tests/platform_test.cpp
  src/platform/encoding.cpp
  src/platform/paths.cpp
)
target_include_directories(md-platform-test PRIVATE src)
target_link_libraries(md-platform-test PRIVATE shell32 ole32)
if(MSVC)
  target_compile_options(md-platform-test PRIVATE /W4)
endif()
add_test(NAME PlatformTest COMMAND md-platform-test)

# Compile the icon directly. This Ninja tree's RC rule calls a cmcldeps wrapper that is not installed.
set(MDV_APP_RES "${CMAKE_CURRENT_BINARY_DIR}/app.res")
add_custom_command(
  OUTPUT "${MDV_APP_RES}"
  COMMAND "${CMAKE_RC_COMPILER}" /nologo /fo "${MDV_APP_RES}" "${CMAKE_SOURCE_DIR}/resources/app.rc"
  DEPENDS "${CMAKE_SOURCE_DIR}/resources/app.rc" "${CMAKE_SOURCE_DIR}/resources/app.ico"
  COMMENT "Compile application icon"
  VERBATIM
)
set_source_files_properties("${MDV_APP_RES}" PROPERTIES
  GENERATED TRUE
  EXTERNAL_OBJECT TRUE
)

add_executable(md-viewer WIN32
  src/main.cpp
  src/app/loader.cpp
  src/app/window.cpp
  src/webview/host.cpp
  src/platform/encoding.cpp
  src/platform/paths.cpp
  src/platform/settings.cpp
  src/platform/watcher.cpp
  resources/app.manifest
  "${MDV_APP_RES}"
)
target_include_directories(md-viewer PRIVATE
  src
  ${WEBVIEW2_DIR}/build/native/include
  ${WIL_DIR}/include
)
target_link_libraries(md-viewer PRIVATE
  mdv_document
  ${WEBVIEW2_DIR}/build/native/x64/WebView2LoaderStatic.lib
  dwmapi
  uxtheme
  user32
  gdi32
  ole32
  oleaut32
  shell32
  shlwapi
  comdlg32
  comctl32
  advapi32
  version
)
target_compile_definitions(md-viewer PRIVATE
  UNICODE
  _UNICODE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  _WIN32_WINNT=0x0A00
  WINVER=0x0A00
)
if(MSVC)
  target_compile_options(md-viewer PRIVATE /W4)
  target_link_options(md-viewer PRIVATE
    "/MANIFEST:EMBED"
    "/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/resources/app.manifest"
  )
endif()

add_custom_command(TARGET md-viewer POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/page
          $<TARGET_FILE_DIR:md-viewer>/assets
  COMMENT "Copy page assets next to md-viewer"
)

add_dependencies(md-viewer md-render-test md-platform-test)
