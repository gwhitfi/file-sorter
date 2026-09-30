#include "DestinationDialog.h"

#include "Button.h"
#include "Theme.h"

namespace {

const wchar_t kClassName[] = L"FileSorter.DestinationDialog";
constexpr int kNameEdit = 100;
constexpr int kPathEdit = 101;
constexpr int kBrowse = 102;
constexpr int kRemove = 103;
constexpr int kMaxNameLength = 40;

struct DialogState {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    HWND nameEdit = nullptr;
    HWND pathEdit = nullptr;
    HWND browse = nullptr;
    HWND remove = nullptr;
    HWND cancel = nullptr;
    HWND save = nullptr;
    int slot = 0;
    bool configured = false;
    std::wstring sortingFolder;
    DestinationDialogResult result;
    bool done = false;
    RECT nameFrame{}, pathFrame{};

    int S(int v) const { return ScaleDpi(v, WindowDpi(hwnd)); }
};

int EditHeight(HWND hwnd) {
    HDC hdc = GetDC(hwnd);
    HGDIOBJ old = SelectObject(hdc, g_fonts.ui);
    TEXTMETRICW tm{};
    GetTextMetricsW(hdc, &tm);
    SelectObject(hdc, old);
    ReleaseDC(hwnd, hdc);
    return tm.tmHeight + 2;
}

void Layout(DialogState* s) {
    RECT rc;
    GetClientRect(s->hwnd, &rc);
    int pad = s->S(24);
    int w = rc.right - pad * 2;
    int frameH = s->S(34);
    int editH = EditHeight(s->hwnd);
    int browseW = s->S(104);

    int y = pad + s->S(48) + s->S(22);  // intro text + label
    s->nameFrame = {pad, y, pad + w, y + frameH};
    y += frameH + s->S(16) + s->S(22);
    s->pathFrame = {pad, y, pad + w - browseW - s->S(8), y + frameH};

    auto placeEdit = [&](HWND edit, const RECT& f) {
        MoveWindow(edit, f.left + s->S(10), f.top + (frameH - editH) / 2, f.right - f.left - s->S(20), editH, TRUE);
    };
    placeEdit(s->nameEdit, s->nameFrame);
    placeEdit(s->pathEdit, s->pathFrame);
    MoveWindow(s->browse, s->pathFrame.right + s->S(8), s->pathFrame.top, browseW, frameH, TRUE);

    int by = rc.bottom - pad - frameH;
    int bw = s->S(100);
    MoveWindow(s->save, rc.right - pad - bw, by, bw, frameH, TRUE);
    MoveWindow(s->cancel, rc.right - pad - bw * 2 - s->S(8), by, bw, frameH, TRUE);
    MoveWindow(s->remove, pad, by, s->S(110), frameH, TRUE);
}

void Paint(DialogState* s, HDC target) {
    RECT rc;
    GetClientRect(s->hwnd, &rc);
    HDC hdc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(hdc, bmp);
    const Palette& p = Theme::Colors();
    Theme::FillRectColor(hdc, rc, p.surface);
    SetBkMode(hdc, TRANSPARENT);

    int pad = s->S(24);
    RECT intro = {pad, pad, rc.right - pad, pad + s->S(44)};
    HGDIOBJ old = SelectObject(hdc, g_fonts.ui);
    SetTextColor(hdc, p.textMuted);
    std::wstring text = L"Files you send with button " + std::to_wstring(s->slot + 1) + L" are moved into this folder \u2014 no copy is left behind. Shortcut: press " + std::to_wstring(s->slot + 1) + L".";
    DrawTextW(hdc, text.c_str(), -1, &intro, DT_WORDBREAK | DT_NOPREFIX);

    auto label = [&](const RECT& frame, const wchar_t* t) {
        RECT lr = {frame.left, frame.top - s->S(24), frame.right, frame.top - s->S(4)};
        SelectObject(hdc, g_fonts.uiBold);
        SetTextColor(hdc, p.text);
        DrawTextW(hdc, t, -1, &lr, DT_SINGLELINE | DT_BOTTOM | DT_NOPREFIX);
    };
    label(s->nameFrame, L"Button name");
    label(s->pathFrame, L"Destination folder");

    UINT dpi = WindowDpi(s->hwnd);
    Theme::DrawInputFrame(hdc, s->nameFrame, GetFocus() == s->nameEdit, dpi);
    Theme::DrawInputFrame(hdc, s->pathFrame, GetFocus() == s->pathEdit, dpi);

    SelectObject(hdc, old);
    BitBlt(target, 0, 0, rc.right, rc.bottom, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(hdc);
}

void Browse(DialogState* s) {
    std::wstring start = Trim(GetWindowTextString(s->pathEdit));
    std::wstring folder = PickFolder(s->hwnd, L"Choose a destination folder", start);
    if (folder.empty()) return;
    SetWindowTextW(s->pathEdit, folder.c_str());
    if (Trim(GetWindowTextString(s->nameEdit)).empty()) {
        std::wstring leaf = FileNameOf(NormalizeDir(folder));
        if (leaf.empty()) leaf = folder;
        SetWindowTextW(s->nameEdit, leaf.substr(0, kMaxNameLength).c_str());
    }
}

void Save(DialogState* s) {
    std::wstring path = NormalizeDir(Trim(GetWindowTextString(s->pathEdit)));
    std::wstring name = Trim(GetWindowTextString(s->nameEdit));
    if (path.empty()) {
        MessageBoxW(s->hwnd, L"Choose the folder files should be moved to.", L"Destination folder needed",
                    MB_OK | MB_ICONINFORMATION);
        SetFocus(s->pathEdit);
        return;
    }
    wchar_t full[MAX_PATH * 4] = {};
    if (GetFullPathNameW(path.c_str(), MAX_PATH * 4, full, nullptr)) path = NormalizeDir(full);
    if (!s->sortingFolder.empty() && SamePath(path, s->sortingFolder)) {
        MessageBoxW(s->hwnd, L"That is the folder you are sorting. Choose a different destination.",
                    L"Choose another folder", MB_OK | MB_ICONWARNING);
        return;
    }
    if (!DirectoryExists(path)) {
        std::wstring msg = L"\u201C" + path + L"\u201D doesn't exist yet.\n\nCreate it now?";
        if (MessageBoxW(s->hwnd, msg.c_str(), L"Create folder?", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        int rc = SHCreateDirectoryExW(s->hwnd, path.c_str(), nullptr);
        if (rc != ERROR_SUCCESS && rc != ERROR_ALREADY_EXISTS) {
            MessageBoxW(s->hwnd, (L"The folder couldn't be created.\n\n" + ErrorText(static_cast<DWORD>(rc))).c_str(),
                        L"File Sorter", MB_OK | MB_ICONERROR);
            return;
        }
    }
    if (name.empty()) {
        name = FileNameOf(path);
        if (name.empty()) name = path;
    }
    s->result.action = DestinationDialogAction::Save;
    s->result.value.name = name.substr(0, kMaxNameLength);
    s->result.value.path = path;
    s->done = true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* s = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        s = static_cast<DialogState*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        s->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (s) Paint(s, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        const Palette& p = Theme::Colors();
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetTextColor(hdc, p.text);
        SetBkColor(hdc, p.input);
        return reinterpret_cast<LRESULT>(Theme::Brush(p.input));
    }
    case WM_COMMAND: {
        if (!s) break;
        int id = LOWORD(wp);
        int code = HIWORD(wp);
        if ((id == kNameEdit || id == kPathEdit) && (code == EN_SETFOCUS || code == EN_KILLFOCUS)) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        switch (id) {
        case IDOK: Save(s); return 0;
        case IDCANCEL: s->done = true; return 0;
        case kBrowse: Browse(s); return 0;
        case kRemove: {
            std::wstring msg = L"Remove destination " + std::to_wstring(s->slot + 1) +
                               L"? The folder itself and its files are not touched.";
            if (MessageBoxW(hwnd, msg.c_str(), L"Remove destination", MB_OKCANCEL | MB_ICONQUESTION) == IDOK) {
                s->result.action = DestinationDialogAction::Remove;
                s->done = true;
            }
            return 0;
        }
        }
        break;
    }
    case WM_CLOSE:
        if (s) s->done = true;
        return 0;
    case WM_DPICHANGED: {
        auto* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        if (s) Layout(s);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

DestinationDialogResult ShowDestinationDialog(HWND owner, int slot, const Destination& current,
                                              const std::wstring& folderBeingSorted) {
    static bool registered = false;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        registered = true;
    }

    DialogState state;
    state.owner = owner;
    state.slot = slot;
    state.configured = current.IsSet();
    state.sortingFolder = folderBeingSorted;

    UINT dpi = WindowDpi(owner);
    RECT client = {0, 0, ScaleDpi(520, dpi), ScaleDpi(292, dpi)};
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    DWORD exStyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
    AdjustWindowRectExForDpi(&client, style, FALSE, exStyle, dpi);
    int w = client.right - client.left, h = client.bottom - client.top;
    RECT ownerRect;
    GetWindowRect(owner, &ownerRect);
    int x = ownerRect.left + (ownerRect.right - ownerRect.left - w) / 2;
    int y = ownerRect.top + (ownerRect.bottom - ownerRect.top - h) / 2;

    std::wstring title = (state.configured ? L"Edit destination " : L"Set up destination ") + std::to_wstring(slot + 1);
    HWND hwnd = CreateWindowExW(exStyle, kClassName, title.c_str(), style, x, y, w, h, owner, nullptr, inst, &state);
    if (!hwnd) return state.result;
    Theme::ApplyTitleBar(hwnd);

    auto makeEdit = [&](int id, const std::wstring& text) {
        HWND e = CreateWindowExW(0, L"EDIT", text.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0,
                                 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
        SendMessageW(e, WM_SETFONT, reinterpret_cast<WPARAM>(g_fonts.ui), FALSE);
        return e;
    };
    state.nameEdit = makeEdit(kNameEdit, current.name);
    SendMessageW(state.nameEdit, EM_SETLIMITTEXT, kMaxNameLength, 0);
    SendMessageW(state.nameEdit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"e.g. Photos"));
    state.pathEdit = makeEdit(kPathEdit, current.path);
    SendMessageW(state.pathEdit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"e.g. C:\\Users\\You\\Pictures"));
    state.browse = Button::Create(hwnd, kBrowse, ButtonKind::Standard, L"Browse\u2026", Glyph::FolderOpen);
    state.remove = Button::Create(hwnd, kRemove, ButtonKind::Subtle, L"Remove", Glyph::Delete, L"Clear this destination slot");
    state.cancel = Button::Create(hwnd, IDCANCEL, ButtonKind::Standard, L"Cancel");
    state.save = Button::Create(hwnd, IDOK, ButtonKind::Primary, L"Save");
    ShowWindow(state.remove, state.configured ? SW_SHOW : SW_HIDE);
    Layout(&state);

    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    SetFocus(current.IsSet() ? state.nameEdit : state.pathEdit);
    if (!current.IsSet()) {
        // Empty slot: jump straight to the folder picker, which is what the user is here to do.
        PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(kBrowse, BN_CLICKED), reinterpret_cast<LPARAM>(state.browse));
    } else {
        SendMessageW(state.nameEdit, EM_SETSEL, 0, -1);
    }

    MSG msg;
    bool quit = false;
    while (!state.done) {
        BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r <= 0) {
            quit = true;
            break;
        }
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    // Re-enable and activate the owner before the dialog disappears, otherwise Windows may hand
    // activation to some other application's window.
    EnableWindow(owner, TRUE);
    ShowWindow(hwnd, SW_HIDE);
    SetForegroundWindow(owner);
    SetActiveWindow(owner);
    DestroyWindow(hwnd);
    if (quit) PostQuitMessage(static_cast<int>(msg.wParam));
    return state.result;
}
