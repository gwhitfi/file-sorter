#include "Button.h"
#include "Theme.h"

namespace {

const wchar_t kClassName[] = L"FileSorter.Button";

struct ButtonData {
    ButtonKind kind = ButtonKind::Standard;
    std::wstring text;
    std::wstring subText;
    std::wstring badge;
    std::wstring tooltip;
    wchar_t glyph = 0;
    bool checked = false;
    bool empty = false;
    bool hover = false;
    bool hoverEdit = false;
    bool pressed = false;
    bool pressedEdit = false;
    bool tracking = false;
};

struct TooltipEntry {
    HWND root;
    HWND tooltip;
};
std::vector<TooltipEntry> g_tooltips;

ButtonData* Data(HWND hwnd) { return reinterpret_cast<ButtonData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)); }

HWND TooltipFor(HWND button) {
    HWND root = GetAncestor(button, GA_ROOT);
    for (auto& t : g_tooltips)
        if (t.root == root && IsWindow(t.tooltip)) return t.tooltip;
    HWND tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, root, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
    SendMessageW(tip, TTM_SETMAXTIPWIDTH, 0, ScaleDpi(420, WindowDpi(root)));
    Theme::ApplyTooltip(tip);
    g_tooltips.push_back({root, tip});
    return tip;
}

void UpdateTool(HWND button, ButtonData* d) {
    HWND tip = TooltipFor(button);
    TTTOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.hwnd = GetParent(button);
    ti.uId = reinterpret_cast<UINT_PTR>(button);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.lpszText = const_cast<wchar_t*>(d->tooltip.c_str());
    if (!SendMessageW(tip, TTM_GETTOOLINFOW, 0, reinterpret_cast<LPARAM>(&ti))) {
        ti.lpszText = const_cast<wchar_t*>(d->tooltip.c_str());
        SendMessageW(tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
    } else {
        ti.lpszText = const_cast<wchar_t*>(d->tooltip.c_str());
        SendMessageW(tip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
    }
}

RECT EditZone(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    UINT dpi = WindowDpi(hwnd);
    rc.left = rc.right - ScaleDpi(40, dpi);
    return rc;
}

SIZE TextSize(HDC hdc, HFONT font, const std::wstring& text) {
    SIZE sz{};
    HGDIOBJ old = SelectObject(hdc, font);
    GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &sz);
    SelectObject(hdc, old);
    return sz;
}

void DrawGlyphAndText(HDC hdc, const RECT& rc, ButtonData* d, COLORREF color, UINT dpi) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    std::wstring glyph = d->glyph ? std::wstring(1, d->glyph) : L"";
    SIZE g = glyph.empty() ? SIZE{0, 0} : TextSize(hdc, g_fonts.icon, glyph);
    SIZE t = d->text.empty() ? SIZE{0, 0} : TextSize(hdc, g_fonts.ui, d->text);
    int gap = (!glyph.empty() && !d->text.empty()) ? ScaleDpi(8, dpi) : 0;
    int total = g.cx + gap + t.cx;
    int x = rc.left + std::max(0, static_cast<int>((rc.right - rc.left - total) / 2));
    if (!glyph.empty()) {
        RECT gr = {x, rc.top, x + g.cx, rc.bottom};
        HGDIOBJ old = SelectObject(hdc, g_fonts.icon);
        DrawTextW(hdc, glyph.c_str(), 1, &gr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SelectObject(hdc, old);
        x += g.cx + gap;
    }
    if (!d->text.empty()) {
        RECT tr = {x, rc.top, rc.right, rc.bottom};
        HGDIOBJ old = SelectObject(hdc, g_fonts.ui);
        DrawTextW(hdc, d->text.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(hdc, old);
    }
}

void PaintTile(HDC hdc, const RECT& rc, ButtonData* d, bool enabled, UINT dpi) {
    const Palette& p = Theme::Colors();
    auto S = [dpi](int v) { return ScaleDpi(v, dpi); };
    RECT box = rc;
    int radius = S(6);

    if (d->empty) {
        COLORREF fill = d->hover ? p.controlHover : p.surface;
        Theme::FillRoundRect(hdc, box, radius, fill, p.surface);
        // Dashed outline marks an unconfigured slot.
        HPEN pen = CreatePen(PS_DOT, 1, p.controlBorder);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, box.left, box.top, box.right, box.bottom, radius * 2, radius * 2);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    } else {
        COLORREF fill = !enabled ? p.control : d->pressed && !d->pressedEdit ? p.controlPressed
                                           : d->hover ? p.controlHover
                                                      : p.control;
        Theme::FillRoundRect(hdc, box, radius, fill, d->hover && enabled ? p.accent : p.controlBorder);
    }

    SetBkMode(hdc, TRANSPARENT);
    int pad = S(12);
    int badge = S(26);
    RECT br = {box.left + pad, (box.top + box.bottom - badge) / 2, box.left + pad + badge, (box.top + box.bottom + badge) / 2};
    if (d->empty) {
        Theme::FillRoundRect(hdc, br, S(4), d->hover ? p.controlHover : p.surface, p.controlBorder);
        SetTextColor(hdc, p.textFaint);
    } else {
        Theme::FillRoundRect(hdc, br, S(4), enabled ? p.accent : p.controlBorder, enabled ? p.accent : p.controlBorder);
        SetTextColor(hdc, enabled ? p.onAccent : p.textFaint);
    }
    HGDIOBJ old = SelectObject(hdc, g_fonts.uiBold);
    DrawTextW(hdc, d->badge.c_str(), -1, &br, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);

    int textLeft = br.right + S(12);
    int textRight = box.right - (d->empty ? pad : S(44));
    if (d->empty) {
        RECT tr = {textLeft, box.top, textRight, box.bottom};
        SelectObject(hdc, g_fonts.ui);
        SetTextColor(hdc, d->hover ? p.text : p.textMuted);
        DrawTextW(hdc, d->text.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    } else {
        int mid = (box.top + box.bottom) / 2;
        RECT tr = {textLeft, box.top + S(4), textRight, mid + S(1)};
        SelectObject(hdc, g_fonts.uiBold);
        SetTextColor(hdc, enabled ? p.text : p.textFaint);
        DrawTextW(hdc, d->text.c_str(), -1, &tr, DT_SINGLELINE | DT_BOTTOM | DT_NOPREFIX | DT_END_ELLIPSIS);
        RECT sr = {textLeft, mid + S(3), textRight, box.bottom - S(4)};
        SelectObject(hdc, g_fonts.small);
        SetTextColor(hdc, p.textMuted);
        std::wstring sub = d->subText;
        DrawTextW(hdc, sub.data(), -1, &sr, DT_SINGLELINE | DT_TOP | DT_NOPREFIX | DT_PATH_ELLIPSIS);

        // Edit (pencil) affordance on the right.
        RECT ez = {box.right - S(40), box.top, box.right, box.bottom};
        RECT dot = {ez.left + S(4), (ez.top + ez.bottom) / 2 - S(15), ez.right - S(6), (ez.top + ez.bottom) / 2 + S(15)};
        if (d->hoverEdit) Theme::FillRoundRect(hdc, dot, S(4), d->pressedEdit ? p.controlPressed : p.surfaceAlt, p.controlBorder);
        SelectObject(hdc, g_fonts.icon);
        SetTextColor(hdc, d->hoverEdit ? p.text : p.textMuted);
        wchar_t g = Glyph::Edit;
        DrawTextW(hdc, &g, 1, &dot, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    }
    SelectObject(hdc, old);
}

void PaintToggle(HDC hdc, const RECT& rc, ButtonData* d, bool enabled, UINT dpi) {
    const Palette& p = Theme::Colors();
    auto S = [dpi](int v) { return ScaleDpi(v, dpi); };
    int box = S(18);
    RECT br = {rc.left + S(2), (rc.top + rc.bottom - box) / 2, rc.left + S(2) + box, (rc.top + rc.bottom + box) / 2};
    if (d->checked) {
        COLORREF fill = d->pressed ? p.accentPressed : d->hover ? p.accentHover : p.accent;
        Theme::FillRoundRect(hdc, br, S(4), fill, fill);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, p.onAccent);
        HFONT checkFont = CreateFontW(-S(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                                      L"Segoe MDL2 Assets");
        HGDIOBJ old = SelectObject(hdc, checkFont);
        wchar_t g = Glyph::Check;
        DrawTextW(hdc, &g, 1, &br, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
        SelectObject(hdc, old);
        DeleteObject(checkFont);
    } else {
        Theme::FillRoundRect(hdc, br, S(4), d->hover ? p.controlHover : p.input, d->hover ? p.text : p.textMuted);
    }
    RECT tr = {br.right + S(8), rc.top, rc.right, rc.bottom};
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, enabled ? p.text : p.textFaint);
    HGDIOBJ old = SelectObject(hdc, g_fonts.ui);
    DrawTextW(hdc, d->text.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(hdc, old);
}

void Paint(HWND hwnd, HDC target) {
    ButtonData* d = Data(hwnd);
    RECT rc;
    GetClientRect(hwnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;
    UINT dpi = WindowDpi(hwnd);
    const Palette& p = Theme::Colors();
    bool enabled = IsWindowEnabled(hwnd) != FALSE;

    HDC hdc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(hdc, bmp);
    Theme::FillRectColor(hdc, rc, p.surface);

    int radius = ScaleDpi(4, dpi);
    switch (d->kind) {
    case ButtonKind::Tile:
        PaintTile(hdc, rc, d, enabled, dpi);
        break;
    case ButtonKind::Toggle:
        PaintToggle(hdc, rc, d, enabled, dpi);
        break;
    default: {
        COLORREF fill = p.control, border = p.controlBorder, fg = p.text;
        if (d->kind == ButtonKind::Primary) {
            fill = d->pressed ? p.accentPressed : d->hover ? p.accentHover : p.accent;
            border = fill;
            fg = p.onAccent;
        } else if (d->kind == ButtonKind::Danger) {
            fill = d->pressed ? p.dangerPressed : d->hover ? p.dangerHover : p.danger;
            border = fill;
            fg = p.onDanger;
        } else if (d->kind == ButtonKind::Subtle) {
            fill = d->pressed ? p.controlPressed : d->hover ? p.controlHover : p.surface;
            border = fill;
        } else {
            fill = d->pressed ? p.controlPressed : d->hover ? p.controlHover : p.control;
        }
        if (!enabled) {
            fill = d->kind == ButtonKind::Subtle ? p.surface : p.control;
            border = d->kind == ButtonKind::Subtle ? p.surface : p.border;
            fg = p.textFaint;
        }
        Theme::FillRoundRect(hdc, rc, radius, fill, border);
        RECT content = rc;
        InflateRect(&content, -ScaleDpi(10, dpi), 0);
        DrawGlyphAndText(hdc, content, d, fg, dpi);
        break;
    }
    }

    BitBlt(target, 0, 0, rc.right, rc.bottom, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(hdc);
}

void Notify(HWND hwnd, WORD code) {
    SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), code), reinterpret_cast<LPARAM>(hwnd));
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ButtonData* d = Data(hwnd);
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        break;
    }
    case WM_NCDESTROY:
        delete d;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        Paint(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ENABLE:
        if (!wp) d->hover = d->pressed = d->hoverEdit = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        if (!d->tracking) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            d->tracking = true;
        }
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT rc;
        GetClientRect(hwnd, &rc);
        bool inside = PtInRect(&rc, pt) != FALSE;
        RECT ez = EditZone(hwnd);
        bool inEdit = d->kind == ButtonKind::Tile && !d->empty && inside && PtInRect(&ez, pt);
        if (inside != d->hover || inEdit != d->hoverEdit) {
            d->hover = inside;
            d->hoverEdit = inEdit;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        d->tracking = false;
        d->hover = d->hoverEdit = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT ez = EditZone(hwnd);
        d->pressed = true;
        d->pressedEdit = d->kind == ButtonKind::Tile && !d->empty && PtInRect(&ez, pt);
        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!d->pressed) return 0;
        bool wasEdit = d->pressedEdit;
        d->pressed = d->pressedEdit = false;
        ReleaseCapture();
        InvalidateRect(hwnd, nullptr, FALSE);
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (PtInRect(&rc, pt)) {
            if (d->kind == ButtonKind::Toggle) {
                d->checked = !d->checked;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            Notify(hwnd, wasEdit ? FSBN_EDIT : static_cast<WORD>(BN_CLICKED));
        }
        return 0;
    }
    case WM_RBUTTONUP:
        if (d->kind == ButtonKind::Tile) Notify(hwnd, FSBN_CONTEXT);
        return 0;
    case WM_CAPTURECHANGED:
        if (d->pressed) {
            d->pressed = d->pressedEdit = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_SETFOCUS:
        // Buttons are pointer-only; keep keyboard focus where the user left it (usually the file list).
        if (wp) SetFocus(reinterpret_cast<HWND>(wp));
        return 0;
    case WM_DPICHANGED_AFTERPARENT:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

namespace Button {

void Register(HINSTANCE instance) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
}

HWND Create(HWND parent, int id, ButtonKind kind, const std::wstring& text, wchar_t glyph, const std::wstring& tooltip) {
    auto* d = new ButtonData;
    d->kind = kind;
    d->text = text;
    d->glyph = glyph;
    d->tooltip = tooltip;
    HWND hwnd = CreateWindowExW(0, kClassName, text.c_str(), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, parent,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), d);
    if (hwnd && !tooltip.empty()) UpdateTool(hwnd, d);
    return hwnd;
}

void SetText(HWND button, const std::wstring& text) {
    if (auto* d = Data(button); d && d->text != text) {
        d->text = text;
        InvalidateRect(button, nullptr, FALSE);
    }
}

void SetSubText(HWND button, const std::wstring& text) {
    if (auto* d = Data(button); d && d->subText != text) {
        d->subText = text;
        InvalidateRect(button, nullptr, FALSE);
    }
}

void SetBadge(HWND button, const std::wstring& badge) {
    if (auto* d = Data(button)) {
        d->badge = badge;
        InvalidateRect(button, nullptr, FALSE);
    }
}

void SetGlyph(HWND button, wchar_t glyph) {
    if (auto* d = Data(button); d && d->glyph != glyph) {
        d->glyph = glyph;
        InvalidateRect(button, nullptr, FALSE);
    }
}

void SetChecked(HWND button, bool checked) {
    if (auto* d = Data(button); d && d->checked != checked) {
        d->checked = checked;
        InvalidateRect(button, nullptr, FALSE);
    }
}

bool GetChecked(HWND button) {
    auto* d = Data(button);
    return d && d->checked;
}

void SetEmpty(HWND button, bool empty) {
    if (auto* d = Data(button); d && d->empty != empty) {
        d->empty = empty;
        InvalidateRect(button, nullptr, FALSE);
    }
}

void SetTooltip(HWND button, const std::wstring& tooltip) {
    if (auto* d = Data(button); d && d->tooltip != tooltip) {
        d->tooltip = tooltip;
        UpdateTool(button, d);
    }
}

int IdealWidth(HWND button) {
    auto* d = Data(button);
    if (!d) return 0;
    UINT dpi = WindowDpi(button);
    HDC hdc = GetDC(button);
    int width = 0;
    if (d->kind == ButtonKind::Toggle) {
        width = ScaleDpi(28, dpi) + TextSize(hdc, g_fonts.ui, d->text).cx + ScaleDpi(4, dpi);
    } else {
        int g = d->glyph ? TextSize(hdc, g_fonts.icon, std::wstring(1, d->glyph)).cx : 0;
        int t = d->text.empty() ? 0 : TextSize(hdc, g_fonts.ui, d->text).cx;
        int gap = (g && t) ? ScaleDpi(8, dpi) : 0;
        int pad = d->text.empty() ? ScaleDpi(10, dpi) : ScaleDpi(16, dpi);
        width = g + gap + t + pad * 2;
    }
    ReleaseDC(button, hdc);
    return width;
}

void RefreshTooltipThemes() {
    for (auto& t : g_tooltips)
        if (IsWindow(t.tooltip)) Theme::ApplyTooltip(t.tooltip);
}

}  // namespace Button
