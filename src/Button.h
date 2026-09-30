// Custom-painted button used everywhere in the UI so it can follow the light/dark theme.
#pragma once
#include "Common.h"

enum class ButtonKind {
    Standard,  // neutral outlined button
    Primary,   // accent-filled call to action
    Danger,    // red destructive action
    Subtle,    // borderless, fills on hover (toolbar style)
    Toggle,    // checkbox with label
    Tile,      // destination tile: badge + title + subtitle + edit affordance
};

// Extra WM_COMMAND notification codes sent by Tile buttons (BN_CLICKED is 0).
constexpr WORD FSBN_EDIT = 0x100;     // edit (pencil) zone clicked
constexpr WORD FSBN_CONTEXT = 0x101;  // right-click

namespace Button {
void Register(HINSTANCE instance);
HWND Create(HWND parent, int id, ButtonKind kind, const std::wstring& text, wchar_t glyph = 0,
            const std::wstring& tooltip = L"");

void SetText(HWND button, const std::wstring& text);
void SetSubText(HWND button, const std::wstring& text);
void SetBadge(HWND button, const std::wstring& badge);
void SetGlyph(HWND button, wchar_t glyph);
void SetChecked(HWND button, bool checked);
bool GetChecked(HWND button);
void SetEmpty(HWND button, bool empty);  // Tile: unconfigured slot
void SetTooltip(HWND button, const std::wstring& tooltip);
int IdealWidth(HWND button);  // width that fits glyph + text with padding

void RefreshTooltipThemes();
}  // namespace Button
