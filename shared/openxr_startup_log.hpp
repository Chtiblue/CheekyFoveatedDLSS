#pragma once
#include <Windows.h>
#include <filesystem>
#include <string>

namespace cheeky {
// Both the layer and support collectors resolve the same process-specific file.
// Creation time prevents a reused PID from pulling in a previous game's log.
inline std::filesystem::path openxr_startup_log_path() {
    wchar_t temp[MAX_PATH + 1]{};
    const auto length = GetTempPathW(static_cast<DWORD>(std::size(temp)), temp);
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!length || length >= std::size(temp) ||
        !GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return {};
    const auto stamp = (static_cast<unsigned long long>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    return std::filesystem::path(temp) / (L"CheekyOpenXR-" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(stamp) + L".log");
}
}
