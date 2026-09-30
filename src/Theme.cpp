#include "Theme.h"

Fonts g_fonts;

namespace {

const Palette kLight = {
    RGB(243, 243, 243),  // window
    RGB(255, 255, 255),  // surface
    RGB(249, 249, 249),  // surfaceAlt
    RGB(224, 224, 224),  // border
    RGB(27, 27, 27),     // text
    RGB(92, 92, 92),     // textMuted
    RGB(138, 138, 138),  // textFaint
    RGB(0, 95, 184),     // accent
    RGB(25, 110, 191),   // accentHover
    RGB(0, 80, 156),     // accentPressed
    RGB(255, 255, 255),  // onAccent
    RGB(196, 43, 28),    // danger
    RGB(178, 38, 25),    // dangerHover
    RGB(150, 32, 21),    // dangerPressed
    RGB(255, 255, 255),  // onDanger
    RGB(251, 251, 251),  // control
    RGB(240, 240, 240),  // controlHover
    RGB(230, 230, 230),  // controlPressed
    RGB(208, 208, 208),  // controlBorder
    RGB(255, 255, 255),  // input
    RGB(255, 255, 255),  // checkerA
    RGB(229, 229, 229),  // checkerB
};

const Palette kDark = {
    RGB(32, 32, 32),     // window
    RGB(43, 43, 43),     // surface
    RGB(50, 50, 50),     // surfaceAlt
    RGB(60, 60, 60),     // border
    RGB(242, 242, 242),  // text
    RGB(176, 176, 176),  // textMuted
    RGB(128, 128, 128),  // textFaint
    RGB(76, 194, 255),   // accent
    RGB(98, 203, 255),   // accentHover
    RGB(60, 165, 220),   // accentPressed
    RGB(0, 0, 0),        // onAccent
    RGB(214, 64, 52),    // danger
    RGB(230, 84, 72),    // dangerHover
    RGB(184, 50, 40),    // dangerPressed
    RGB(255, 255, 255),  // onDanger
    RGB(58, 58, 58),     // control
    RGB(68, 68, 68),     // controlHover
    RGB(50, 50, 50),     // controlPressed
    RGB(82, 82, 82),     // controlBorder
    RGB(31, 31, 31),     // input
    RGB(58, 58, 58),     // checkerA
    RGB(46, 46, 46),     // checkerB
};

bool g_dark = false;
std::vector<std::pair<COLORREF, HBRUSH>> g_brushes;
DWORD g_build = 0;

// Undocumented uxtheme exports used by Explorer, Notepad, etc. to get dark scrollbars,
// dark context menus and dark tooltips. Loaded by ordinal and only on builds that have them.
enum class PreferredAppMode { Default, AllowDark, ForceDark, ForceLight, Max };
using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode);
using AllowDarkModeForAppFn = BOOL(WINAPI*)(BOOL);
using AllowDarkModeForWindowFn = BOOL(WINAPI*)(HWND, BOOL);
using FlushMenuThemesFn = void(WINAPI*)();

SetPreferredAppModeFn pSetPreferredAppMode = nullptr;
AllowDarkModeForAppFn pAllowDarkModeForApp = nullptr;
AllowDarkModeForWindowFn pAllowDarkModeForWindow = nullptr;
FlushMenuThemesFn pFlushMenuThemes = nullptr;

DWORD QueryBuildNumber() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto fn = ntdll ? reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion"))) : nullptr;
    OSVERSIONINFOW vi{sizeof(vi)};
    if (fn && fn(&vi) == 0) return vi.dwBuildNumber;
    return 0;
}

template <class Fn>
Fn LoadOrdinal(HMODULE module, WORD ordinal) {
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(module, MAKEINTRESOURCEA(ordinal))));
}

void ApplyAppMode() {
    if (pSetPreferredAppMode)
        pSetPreferredAppMode(g_dark ? PreferredAppMode::ForceDark : PreferredAppMode::ForceLight);
    else if (pAllowDarkModeForApp)
        pAllowDarkModeForApp(g_dark);
    if (pFlushMenuThemes) pFlushMenuThemes();
}

bool FontExists(const wchar_t* face) {
    HDC hdc = GetDC(nullptr);
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(lf.lfFaceName, face, _TRUNCATE);
    bool found = false;
    EnumFontFamiliesExW(
        hdc, &lf,
        [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM lp) -> int {
            *reinterpret_cast<bool*>(lp) = true;
            return 0;
        },
        reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, hdc);
    return found;
}

HFONT MakeFont(const wchar_t* face, int px, int weight) {
    return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

}  // namespace

namespace Theme {

void Initialize() {
    g_build = QueryBuildNumber();
    if (g_build >= 17763) {  // Windows 10 1809+
        if (HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
            pAllowDarkModeForWindow = LoadOrdinal<AllowDarkModeForWindowFn>(ux, 133);
            pFlushMenuThemes = LoadOrdinal<FlushMenuThemesFn>(ux, 136);
            if (g_build >= 18362)
                pSetPreferredAppMode = LoadOrdinal<SetPreferredAppModeFn>(ux, 135);
            else
                pAllowDarkModeForApp = LoadOrdinal<AllowDarkModeForAppFn>(ux, 135);
        }
    }
}

bool SystemPrefersDark() {
    DWORD value = 1, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS)
        return value == 0;
    return false;
}

bool IsDark() { return g_dark; }

void SetDark(bool dark) {
    g_dark = dark;
    ApplyAppMode();
}

const Palette& Colors() { return g_dark ? kDark : kLight; }

HBRUSH Brush(COLORREF color) {
    for (auto& b : g_brushes)
        if (b.first == color) return b.second;
    HBRUSH brush = CreateSolidBrush(color);
    g_brushes.emplace_back(color, brush);
    return brush;
}

void ApplyTitleBar(HWND hwnd) {
    BOOL dark = g_dark ? TRUE : FALSE;
    // Attribute 20 is DWMWA_USE_IMMERSIVE_DARK_MODE; builds before 20H1 used 19.
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark))))
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    if (g_build >= 22000) {  // Windows 11: tint the caption to match the window.
        COLORREF caption = Colors().window;
        COLORREF text = Colors().text;
        DwmSetWindowAttribute(hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &caption, sizeof(caption));
        DwmSetWindowAttribute(hwnd, 36 /*DWMWA_TEXT_COLOR*/, &text, sizeof(text));
    }
    if (pAllowDarkModeForWindow) pAllowDarkModeForWindow(hwnd, dark);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void ApplyControl(HWND hwnd, const wchar_t* lightClass, const wchar_t* darkClass) {
    if (!hwnd) return;
    if (pAllowDarkModeForWindow) pAllowDarkModeForWindow(hwnd, g_dark);
    SetWindowTheme(hwnd, g_dark ? darkClass : lightClass, nullptr);
    SendMessageW(hwnd, WM_THEMECHANGED, 0, 0);
}

void ApplyTooltip(HWND tooltip) {
    if (!tooltip) return;
    if (pAllowDarkModeForWindow) pAllowDarkModeForWindow(tooltip, g_dark);
    SetWindowTheme(tooltip, g_dark ? L"DarkMode_Explorer" : nullptr, nullptr);
}

void FillRectColor(HDC hdc, const RECT& rc, COLORREF color) { FillRect(hdc, &rc, Brush(color)); }

void FrameRectColor(HDC hdc, const RECT& rc, COLORREF color, int thickness) {
    HBRUSH b = Brush(color);
    RECT r = rc;
    for (int i = 0; i < thickness; ++i) {
        FrameRect(hdc, &r, b);
        InflateRect(&r, -1, -1);
    }
}

void FillRoundRect(HDC hdc, const RECT& rc, int radius, COLORREF fill, COLORREF border) {
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, Brush(fill));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius * 2, radius * 2);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void DrawInputFrame(HDC hdc, const RECT& rc, bool focused, UINT dpi) {
    const Palette& p = Colors();
    FillRoundRect(hdc, rc, ScaleDpi(4, dpi), p.input, focused ? p.accent : p.controlBorder);
    if (focused) {
        // Windows 11 style: thicker accent underline on the focused field.
        RECT line = {rc.left + 1, rc.bottom - ScaleDpi(2, dpi), rc.right - 1, rc.bottom - 1};
        FillRectColor(hdc, line, p.accent);
    }
}

}  // namespace Theme

void Fonts::Create(UINT forDpi) {
    Destroy();
    dpi = forDpi;
    static const wchar_t* uiFace = nullptr;
    static const wchar_t* monoFace = nullptr;
    static const wchar_t* iconFace = nullptr;
    if (!uiFace) {
        uiFace = FontExists(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
        monoFace = FontExists(L"Cascadia Mono") ? L"Cascadia Mono" : L"Consolas";
        iconFace = FontExists(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    }
    auto px = [&](int v) { return ScaleDpi(v, forDpi); };
    ui = MakeFont(uiFace, px(14), FW_NORMAL);
    uiBold = MakeFont(uiFace, px(14), FW_SEMIBOLD);
    small = MakeFont(uiFace, px(12), FW_NORMAL);
    title = MakeFont(uiFace, px(16), FW_SEMIBOLD);
    mono = MakeFont(monoFace, px(13), FW_NORMAL);
    icon = MakeFont(iconFace, px(16), FW_NORMAL);
    iconLarge = MakeFont(iconFace, px(44), FW_NORMAL);
}

void Fonts::Destroy() {
    for (HFONT* f : {&ui, &uiBold, &small, &title, &mono, &icon, &iconLarge}) {
        if (*f) DeleteObject(*f);
        *f = nullptr;
    }
}
