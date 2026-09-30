// Shared Windows includes, small utilities and app-wide constants.
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <dwmapi.h>

#include <algorithm>
#include <cwchar>
#include <string>
#include <utility>
#include <vector>

// Private window messages.
constexpr UINT WM_APP_OPDONE = WM_APP + 1;        // lParam = FileOp*
constexpr UINT WM_APP_OPPROGRESS = WM_APP + 2;    // wParam = percent
constexpr UINT WM_APP_RESTOREFOCUS = WM_APP + 3;  // give focus back to the file list
constexpr UINT WM_APP_MEDIAEVENT = WM_APP + 4;    // MFPlay event, posted to the preview pane

constexpr int kMaxDestinations = 5;

// Minimal COM smart pointer (avoids a dependency on WRL/ATL so both MSVC and MinGW build).
template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(const ComPtr& o) : p_(o.p_) { if (p_) p_->AddRef(); }
    ComPtr(ComPtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    ~ComPtr() { Reset(); }
    ComPtr& operator=(ComPtr o) noexcept { std::swap(p_, o.p_); return *this; }

    void Reset() { if (p_) { T* p = p_; p_ = nullptr; p->Release(); } }
    T* Get() const { return p_; }
    T** Put() { Reset(); return &p_; }
    void** PutVoid() { return reinterpret_cast<void**>(Put()); }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }

    template <class U>
    HRESULT As(ComPtr<U>& out) const {
        if (!p_) return E_POINTER;
        return p_->QueryInterface(__uuidof(U), out.PutVoid());
    }

private:
    T* p_ = nullptr;
};

inline int ScaleDpi(int value, UINT dpi) { return MulDiv(value, static_cast<int>(dpi), 96); }

inline UINT WindowDpi(HWND hwnd) {
    UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 0;
    return dpi ? dpi : 96;
}

inline std::wstring ToLower(std::wstring s) {
    if (!s.empty()) CharLowerBuffW(s.data(), static_cast<DWORD>(s.size()));
    return s;
}

inline std::wstring Trim(const std::wstring& s) {
    const wchar_t* ws = L" \t\r\n\"";
    size_t b = s.find_first_not_of(ws);
    if (b == std::wstring::npos) return L"";
    size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

inline std::wstring FormatBytes(unsigned long long n) {
    wchar_t buf[64] = {};
    if (FAILED(StrFormatByteSizeEx(n, SFBS_FLAGS_ROUND_TO_NEAREST_DISPLAYED_DIGIT, buf, 64)))
        swprintf(buf, 64, L"%llu bytes", n);
    return buf;
}

inline std::wstring FormatNumber(unsigned long long n) {
    std::wstring digits = std::to_wstring(n);
    std::wstring out;
    int count = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (count && count % 3 == 0) out.insert(out.begin(), L',');
        out.insert(out.begin(), *it);
        ++count;
    }
    return out;
}

inline std::wstring FormatFileTime(const FILETIME& ft) {
    if (ft.dwLowDateTime == 0 && ft.dwHighDateTime == 0) return L"";
    SYSTEMTIME utc{}, local{};
    if (!FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return L"";
    wchar_t date[80] = {}, time[80] = {};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, date, 80, nullptr);
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &local, nullptr, time, 80);
    return std::wstring(date) + L"  " + time;
}

inline std::wstring ErrorText(DWORD code) {
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&msg), 0, nullptr);
    std::wstring text = msg ? msg : L"";
    if (msg) LocalFree(msg);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' ')) text.pop_back();
    if (text.empty()) text = L"Error " + std::to_wstring(code);
    return text;
}

inline std::wstring JoinPath(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    if (dir.back() == L'\\' || dir.back() == L'/') return dir + name;
    return dir + L"\\" + name;
}

inline std::wstring FileNameOf(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

// "C:\a\b\" -> "C:\a\b", while keeping roots such as "C:\" intact.
inline std::wstring NormalizeDir(std::wstring path) {
    while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
    if (path.size() == 2 && path[1] == L':') path += L'\\';
    return path;
}

inline std::wstring ParentDir(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L"";
    return NormalizeDir(path.substr(0, pos + 1));
}

inline bool SamePath(const std::wstring& a, const std::wstring& b) {
    std::wstring x = NormalizeDir(a), y = NormalizeDir(b);
    return CompareStringOrdinal(x.c_str(), -1, y.c_str(), -1, TRUE) == CSTR_EQUAL;
}

inline bool DirectoryExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

inline bool PathExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

inline std::wstring GetWindowTextString(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    std::wstring s(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(hwnd, s.data(), len + 1);
    s.resize(static_cast<size_t>(len));
    return s;
}

// Shows the standard folder picker. Returns an empty string when cancelled.
inline std::wstring PickFolder(HWND owner, const std::wstring& title, const std::wstring& startIn) {
    std::wstring result;
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dlg.Put()))))
        return result;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    dlg->SetTitle(title.c_str());
    if (!startIn.empty() && DirectoryExists(startIn)) {
        ComPtr<IShellItem> folder;
        if (SUCCEEDED(SHCreateItemFromParsingName(startIn.c_str(), nullptr, IID_PPV_ARGS(folder.Put()))))
            dlg->SetFolder(folder.Get());
    }
    if (FAILED(dlg->Show(owner))) return result;
    ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(item.Put()))) return result;
    PWSTR path = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        result = path;
        CoTaskMemFree(path);
    }
    return result;
}
