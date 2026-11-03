#pragma once
// Windows taskbar identity: one explicit AppUserModelID for the launcher (process, window and Jump List),
// so the taskbar button, its pin and the Jump List always belong to launchme.exe and show its icon, never
// the icon of a game started from it.
#ifdef _WIN32
#include <Windows.h>
#include <propkey.h>
#include <propsys.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <string>

namespace app_identity {
inline constexpr const wchar_t* id = L"uxandai.launchme";

inline void setProcess() { SetCurrentProcessExplicitAppUserModelID(id); }

// The window's own properties: the same ID, and what a pin or a relaunch from the taskbar runs and shows.
inline void setWindow(HWND window) {
    using Microsoft::WRL::ComPtr;
    wchar_t path[MAX_PATH * 2]{};
    if (!GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)))) return;
    ComPtr<IPropertyStore> store;
    if (FAILED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(store.GetAddressOf())))) return;
    const auto put = [&](const PROPERTYKEY& key, const std::wstring& text) {
        PROPVARIANT value{};
        value.vt = VT_LPWSTR;
        value.pwszVal = static_cast<LPWSTR>(CoTaskMemAlloc((text.size() + 1) * sizeof(wchar_t)));
        if (!value.pwszVal) return;
        std::copy(text.c_str(), text.c_str() + text.size() + 1, value.pwszVal);
        store->SetValue(key, value);
        PropVariantClear(&value);
    };
    const std::wstring exe = path;
    put(PKEY_AppUserModel_ID, id);
    put(PKEY_AppUserModel_RelaunchCommand, L"\"" + exe + L"\"");
    put(PKEY_AppUserModel_RelaunchIconResource, exe + L",0");
    put(PKEY_AppUserModel_RelaunchDisplayNameResource, L"launchme");
    store->Commit();
}
} // namespace app_identity
#endif
