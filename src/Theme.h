// Light/dark palettes, fonts and the helpers that make stock Win32 controls follow the theme.
#pragma once
#include "Common.h"

struct Palette {
    COLORREF window;        // app background behind the panes
    COLORREF surface;       // pane background
    COLORREF surfaceAlt;    // subtle fills (info strips, hover rows)
    COLORREF border;        // pane borders and dividers
    COLORREF text;
    COLORREF textMuted;
    COLORREF textFaint;     // hidden files, disabled text
    COLORREF accent;
    COLORREF accentHover;
    COLORREF accentPressed;
    COLORREF onAccent;
    COLORREF danger;
    COLORREF dangerHover;
    COLORREF dangerPressed;
    COLORREF onDanger;
    COLORREF control;       // neutral button fill
    COLORREF controlHover;
    COLORREF controlPressed;
    COLORREF controlBorder;
    COLORREF input;         // text box background
    COLORREF checkerA;      // transparency checkerboard
    COLORREF checkerB;
};

namespace Theme {
void Initialize();
bool SystemPrefersDark();
bool IsDark();
void SetDark(bool dark);
const Palette& Colors();

HBRUSH Brush(COLORREF color);  // cached; owned by Theme

// Title bar / frame colours for a top-level window.
void ApplyTitleBar(HWND hwnd);
// Scrollbars and other visual-styled parts of a stock control.
void ApplyControl(HWND hwnd, const wchar_t* lightClass = L"Explorer", const wchar_t* darkClass = L"DarkMode_Explorer");
void ApplyTooltip(HWND tooltip);

// Drawing helpers shared by the custom-painted surfaces.
void FillRectColor(HDC hdc, const RECT& rc, COLORREF color);
void FrameRectColor(HDC hdc, const RECT& rc, COLORREF color, int thickness = 1);
void FillRoundRect(HDC hdc, const RECT& rc, int radius, COLORREF fill, COLORREF border);
void DrawInputFrame(HDC hdc, const RECT& rc, bool focused, UINT dpi);
}  // namespace Theme

// DPI-dependent fonts. Recreated when the window moves between monitors.
struct Fonts {
    HFONT ui = nullptr;         // body text
    HFONT uiBold = nullptr;     // section titles, emphasised text
    HFONT small = nullptr;      // secondary text
    HFONT title = nullptr;      // file name in the preview pane
    HFONT mono = nullptr;       // text / hex preview
    HFONT icon = nullptr;       // Segoe Fluent Icons / MDL2 glyphs
    HFONT iconLarge = nullptr;  // large placeholder glyphs
    UINT dpi = 0;

    void Create(UINT forDpi);
    void Destroy();
};

extern Fonts g_fonts;

// Glyphs shared by Segoe Fluent Icons (Windows 11) and Segoe MDL2 Assets (Windows 10).
namespace Glyph {
constexpr wchar_t Folder = 0xE8B7;
constexpr wchar_t FolderOpen = 0xE838;
constexpr wchar_t Refresh = 0xE72C;
constexpr wchar_t Delete = 0xE74D;
constexpr wchar_t Undo = 0xE7A7;
constexpr wchar_t Next = 0xE893;
constexpr wchar_t Edit = 0xE70F;
constexpr wchar_t Add = 0xE710;
constexpr wchar_t Play = 0xE768;
constexpr wchar_t Pause = 0xE769;
constexpr wchar_t Columns = 0xE8FD;
constexpr wchar_t OpenExternal = 0xE8A7;
constexpr wchar_t Check = 0xE73E;
constexpr wchar_t Document = 0xE8A5;
constexpr wchar_t Completed = 0xE930;
constexpr wchar_t Preview = 0xE890;
constexpr wchar_t Warning = 0xE7BA;
constexpr wchar_t Moon = 0xE708;
}  // namespace Glyph
