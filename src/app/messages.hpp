#pragma once

#include <windows.h>

namespace mdv {

inline constexpr UINT WM_APP_PARSED = WM_APP + 1;
inline constexpr UINT WM_APP_FILECHANGED = WM_APP + 2;
inline constexpr UINT_PTR kTimerReload = 1;
inline constexpr UINT_PTR kTimerSave = 2;

}  // namespace mdv
