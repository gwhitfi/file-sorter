#include "App.h"

#include "Button.h"
#include "DestinationDialog.h"
#include "Theme.h"

namespace {

const wchar_t kClassName[] = L"FileSorter.Main";
const wchar_t kAppName[] = L"File Sorter";

enum : int {
    IDC_PATH = 100,
    IDC_PATH_GO,
    IDC_BROWSE,
    IDC_THEME,
    IDC_REFRESH,
    IDC_COLUMNS,
    IDC_LIST,
    IDC_PREVIEW,
    IDC_OPEN,
    IDC_DELETE,
    IDC_SKIP,
    IDC_UNDO,
    IDC_CONFIRM,
    IDC_DEST0 = 150,  // .. IDC_DEST0 + 4

    ID_KEY_DEST0 = 200,  // .. ID_KEY_DEST0 + 4
    ID_KEY_DELETE = 210,
    ID_KEY_DELETE_PERMANENT,
    ID_KEY_SKIP,
    ID_KEY_UNDO,
    ID_KEY_BROWSE,
    ID_KEY_REFRESH,
    ID_KEY_THEME,
    ID_KEY_OPEN,

    ID_COLUMN0 = 300,  // .. ID_COLUMN0 + COL_COUNT

    ID_MENU_EDIT = 400,
    ID_MENU_OPEN_FOLDER,
    ID_MENU_REMOVE,
};

// Layout metrics in 96-DPI pixels.
constexpr int kGap = 8;
constexpr int kHeader = 44;
constexpr int kPad = 14;
constexpr int kStatus = 30;
constexpr int kTileHeight = 64;
constexpr int kTileGap = 10;
constexpr int kActionHeight = 36;
constexpr int kTileMinWidth = 180;

std::wstring Quote(const std::wstring& s) { return L"“" + s + L"”"; }

std::wstring UniqueTarget(const std::wstring& dir, const std::wstring& name) {
    std::wstring candidate = JoinPath(dir, name);
    if (!PathExists(candidate)) return candidate;
    const wchar_t* ext = PathFindExtensionW(name.c_str());
    std::wstring stem = name.substr(0, static_cast<size_t>(ext - name.c_str()));
    for (int i = 2; i < 10000; ++i) {
        candidate = JoinPath(dir, stem + L" (" + std::to_wstring(i) + L")" + ext);
        if (!PathExists(candidate)) return candidate;
    }
    return L"";
}

bool SameFileData(const std::wstring& a, const std::wstring& b) {
    WIN32_FILE_ATTRIBUTE_DATA x{}, y{};
    return GetFileAttributesExW(a.c_str(), GetFileExInfoStandard, &x) &&
           GetFileAttributesExW(b.c_str(), GetFileExInfoStandard, &y) && x.nFileSizeHigh == y.nFileSizeHigh &&
           x.nFileSizeLow == y.nFileSizeLow && CompareFileTime(&x.ftLastWriteTime, &y.ftLastWriteTime) == 0;
}

}  // namespace

// A move (sort or undo) running on a worker thread so large cross-drive moves never freeze the UI.
struct FileOp {
    enum class Kind { Sort, Undo } kind = Kind::Sort;
    HWND notify = nullptr;
    std::wstring source;
    std::wstring target;
    std::wstring label;  // destination name ("Photos") or original folder for undo
    DWORD error = 0;
    bool sourceLeftBehind = false;
    volatile LONG lastPercent = -1;
};

namespace {

DWORD CALLBACK MoveProgress(LARGE_INTEGER total, LARGE_INTEGER done, LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD,
                            HANDLE, HANDLE, LPVOID context) {
    auto* op = static_cast<FileOp*>(context);
    if (total.QuadPart > 0) {
        LONG pct = static_cast<LONG>(done.QuadPart * 100 / total.QuadPart);
        if (pct != op->lastPercent) {
            op->lastPercent = pct;
            PostMessageW(op->notify, WM_APP_OPPROGRESS, static_cast<WPARAM>(pct), 0);
        }
    }
    return PROGRESS_CONTINUE;
}

DWORD WINAPI MoveThread(LPVOID param) {
    auto* op = static_cast<FileOp*>(param);
    for (int attempt = 0;; ++attempt) {
        if (MoveFileWithProgressW(op->source.c_str(), op->target.c_str(), MoveProgress, op,
                                  MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH)) {
            op->error = 0;
            break;
        }
        op->error = GetLastError();
        // Preview hosts can take a moment to let go of the file after being unloaded.
        bool transient = op->error == ERROR_SHARING_VIOLATION || op->error == ERROR_LOCK_VIOLATION;
        if (!transient || attempt >= 12) break;
        Sleep(200);
    }
    if (op->error == 0 && PathExists(op->source) && SameFileData(op->source, op->target)) {
        // Cross-drive moves copy then delete; if the delete failed (e.g. read-only), finish it so no copy is kept.
        DWORD attrs = GetFileAttributesW(op->source.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY))
            SetFileAttributesW(op->source.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
        if (!DeleteFileW(op->source.c_str())) op->sourceLeftBehind = true;
    }
    PostMessageW(op->notify, WM_APP_OPDONE, 0, reinterpret_cast<LPARAM>(op));
    return 0;
}

}  // namespace

// =============================================================================================
// Creation

bool App::Create(HINSTANCE instance, int showCommand, const std::wstring& initialFolder) {
    instance_ = instance;
    settings_.Load();
    Theme::Initialize();
    bool dark = settings_.theme == ThemeMode::Dark ||
                (settings_.theme == ThemeMode::System && Theme::SystemPrefersDark());
    Theme::SetDark(dark);
    Button::Register(instance);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON), 0));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return false;

    hwnd_ = CreateWindowExW(WS_EX_ACCEPTFILES, kClassName, kAppName, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, 1280, 820, nullptr, nullptr, instance, this);
    if (!hwnd_) return false;

    ACCEL keys[] = {
        {FVIRTKEY, '1', ID_KEY_DEST0},     {FVIRTKEY, '2', ID_KEY_DEST0 + 1},
        {FVIRTKEY, '3', ID_KEY_DEST0 + 2}, {FVIRTKEY, '4', ID_KEY_DEST0 + 3},
        {FVIRTKEY, '5', ID_KEY_DEST0 + 4}, {FVIRTKEY, VK_NUMPAD1, ID_KEY_DEST0},
        {FVIRTKEY, VK_NUMPAD2, ID_KEY_DEST0 + 1}, {FVIRTKEY, VK_NUMPAD3, ID_KEY_DEST0 + 2},
        {FVIRTKEY, VK_NUMPAD4, ID_KEY_DEST0 + 3}, {FVIRTKEY, VK_NUMPAD5, ID_KEY_DEST0 + 4},
        {FVIRTKEY, VK_DELETE, ID_KEY_DELETE},
        {FVIRTKEY | FSHIFT, VK_DELETE, ID_KEY_DELETE_PERMANENT},
        {FVIRTKEY, VK_SPACE, ID_KEY_SKIP},
        {FVIRTKEY | FCONTROL, 'Z', ID_KEY_UNDO},
        {FVIRTKEY | FCONTROL, 'O', ID_KEY_BROWSE},
        {FVIRTKEY, VK_F5, ID_KEY_REFRESH},
        {FVIRTKEY | FCONTROL, 'T', ID_KEY_THEME},
        {FVIRTKEY | FCONTROL, VK_RETURN, ID_KEY_OPEN},
    };
    accelerators_ = CreateAcceleratorTableW(keys, static_cast<int>(std::size(keys)));

    RestoreWindowPlacement(showCommand);
    UpdateWindow(hwnd_);

    std::wstring folder = !initialFolder.empty() ? initialFolder : settings_.lastFolder;
    if (!folder.empty() && DirectoryExists(folder)) {
        LoadFolder(folder);
    } else if (!initialFolder.empty() && PathExists(initialFolder)) {
        LoadFolder(ParentDir(initialFolder), FileNameOf(initialFolder));
    } else {
        UpdatePreview();
        SetStatus(L"Choose a folder to sort: click Browse, press Ctrl+O, or drop a folder onto this window.");
    }
    SetFocus(list_.Hwnd());
    return true;
}

void App::RestoreWindowPlacement(int showCommand) {
    RECT r = settings_.windowRect;
    bool valid = r.right - r.left >= 400 && r.bottom - r.top >= 300 && MonitorFromRect(&r, MONITOR_DEFAULTTONULL);
    if (!valid) {
        HMONITOR mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(mon, &mi);
        RECT work = mi.rcWork;
        int w = std::min(S(1320), static_cast<int>(work.right - work.left) - S(40));
        int h = std::min(S(860), static_cast<int>(work.bottom - work.top) - S(40));
        r = {work.left + (work.right - work.left - w) / 2, work.top + (work.bottom - work.top - h) / 2, 0, 0};
        r.right = r.left + w;
        r.bottom = r.top + h;
    }
    WINDOWPLACEMENT wp{sizeof(wp)};
    GetWindowPlacement(hwnd_, &wp);
    wp.rcNormalPosition = r;
    wp.showCmd = settings_.maximized ? SW_SHOWMAXIMIZED
                                     : (showCommand == SW_SHOWMINIMIZED || showCommand == SW_MINIMIZE ? SW_SHOWMINIMIZED : SW_SHOWNORMAL);
    SetWindowPlacement(hwnd_, &wp);
}

void App::OnCreate() {
    dpi_ = WindowDpi(hwnd_);
    g_fonts.Create(dpi_);
    Theme::ApplyTitleBar(hwnd_);

    pathEdit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PATH)), instance_, nullptr);
    SendMessageW(pathEdit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Choose a folder to sort…"));
    SetWindowSubclass(pathEdit_, PathEditProc, 1, reinterpret_cast<DWORD_PTR>(this));

    browseButton_ = Button::Create(hwnd_, IDC_BROWSE, ButtonKind::Primary, L"Browse…", Glyph::FolderOpen,
                                   L"Choose the folder to sort (Ctrl+O)");
    themeButton_ = Button::Create(hwnd_, IDC_THEME, ButtonKind::Toggle, L"Dark mode", 0, L"Switch between light and dark mode (Ctrl+T)");
    Button::SetChecked(themeButton_, Theme::IsDark());
    refreshButton_ = Button::Create(hwnd_, IDC_REFRESH, ButtonKind::Subtle, L"Refresh", Glyph::Refresh,
                                    L"Re-read the folder (F5)");
    columnsButton_ = Button::Create(hwnd_, IDC_COLUMNS, ButtonKind::Subtle, L"Columns", Glyph::Columns,
                                    L"Choose which details to show");
    openButton_ = Button::Create(hwnd_, IDC_OPEN, ButtonKind::Subtle, L"Open", Glyph::OpenExternal,
                                 L"Open the file in its default app (Enter or double-click)");

    list_.Create(hwnd_, IDC_LIST);
    list_.Configure(settings_.columnMask, settings_.columnWidths, settings_.sortColumn, settings_.sortAscending);
    preview_.Create(hwnd_, IDC_PREVIEW);

    for (int i = 0; i < kMaxDestinations; ++i) {
        destButtons_[i] = Button::Create(hwnd_, IDC_DEST0 + i, ButtonKind::Tile, L"");
        Button::SetBadge(destButtons_[i], std::to_wstring(i + 1));
    }
    deleteButton_ = Button::Create(hwnd_, IDC_DELETE, ButtonKind::Danger, L"Delete", Glyph::Delete,
                                   L"Move the file to the Recycle Bin (Del).\nShift+Del deletes permanently.");
    skipButton_ = Button::Create(hwnd_, IDC_SKIP, ButtonKind::Standard, L"Skip", Glyph::Next,
                                 L"Leave this file here and go to the next one (Space)");
    undoButton_ = Button::Create(hwnd_, IDC_UNDO, ButtonKind::Standard, L"Undo", Glyph::Undo,
                                 L"Move the last sorted file back (Ctrl+Z)");
    confirmButton_ = Button::Create(hwnd_, IDC_CONFIRM, ButtonKind::Toggle, L"Ask before deleting", 0,
                                    L"Show a confirmation before sending files to the Recycle Bin");
    Button::SetChecked(confirmButton_, settings_.confirmDelete);

    UpdateFonts();
    list_.ApplyTheme();
    UpdateDestinationButtons();
    UpdateSummary();
    UpdateActionStates();
}

void App::UpdateFonts() {
    SendMessageW(pathEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(g_fonts.ui), TRUE);
    list_.SetFont(g_fonts.ui);
    preview_.OnFontsChanged();
}

// =============================================================================================
// Layout

int App::LeftTopMinHeight() const { return S(kHeader) + S(34) + S(8) + S(20) + S(kPad); }

int App::DestinationColumns(int width) const {
    int inner = width - S(kPad) * 2;
    for (int cols : {5, 3, 2})
        if (inner >= cols * S(kTileMinWidth) + (cols - 1) * S(kTileGap)) return cols;
    return 1;
}

int App::DestinationMinHeight(int width) const {
    int cols = DestinationColumns(width);
    int rows = (kMaxDestinations + cols - 1) / cols;
    return S(kHeader) + rows * S(kTileHeight) + (rows - 1) * S(kTileGap) + S(16) + S(kActionHeight) + S(kPad);
}

void App::Layout() {
    RECT client;
    GetClientRect(hwnd_, &client);
    if (client.right <= 0 || client.bottom <= 0) return;

    int g = S(kGap);
    rcStatus_ = {0, client.bottom - S(kStatus), client.right, client.bottom};
    rcArea_ = {g, g, client.right - g, rcStatus_.top};
    int areaW = rcArea_.right - rcArea_.left;
    int areaH = rcArea_.bottom - rcArea_.top;

    // Vertical split between the left (folder + files) and right (preview + destinations) halves.
    int usable = areaW - g;
    int minSide = std::min(S(340), usable / 2);
    int leftW = std::clamp(static_cast<int>(usable * settings_.mainSplit + 0.5), minSide, usable - minSide);
    int rightLeft = rcArea_.left + leftW + g;

    int topMin = LeftTopMinHeight();
    int topH = settings_.leftTopHeight ? S(settings_.leftTopHeight) : topMin;
    topH = std::clamp(topH, topMin, std::max(topMin, areaH - g - S(160)));
    rcFolder_ = {rcArea_.left, rcArea_.top, rcArea_.left + leftW, rcArea_.top + topH};
    rcFiles_ = {rcArea_.left, rcFolder_.bottom + g, rcArea_.left + leftW, rcArea_.bottom};

    int rightW = rcArea_.right - rightLeft;
    int destMin = DestinationMinHeight(rightW);
    int destH = settings_.destHeight ? S(settings_.destHeight) : destMin;
    destH = std::clamp(destH, destMin, std::max(destMin, areaH - g - S(200)));
    rcDest_ = {rightLeft, rcArea_.bottom - destH, rcArea_.right, rcArea_.bottom};
    rcPreview_ = {rightLeft, rcArea_.top, rcArea_.right, rcDest_.top - g};

    HDWP dwp = BeginDeferWindowPos(24);
    auto place = [&](HWND h, int x, int y, int w, int hgt) {
        if (h) dwp = DeferWindowPos(dwp, h, nullptr, x, y, std::max(0, w), std::max(0, hgt), SWP_NOZORDER | SWP_NOACTIVATE);
    };
    int pad = S(kPad);
    int headerBtnH = S(32);
    auto headerY = [&](const RECT& pane) { return pane.top + (S(kHeader) - headerBtnH) / 2 + S(2); };

    // Folder pane.
    int themeW = Button::IdealWidth(themeButton_);
    place(themeButton_, rcFolder_.right - pad - themeW, headerY(rcFolder_), themeW, headerBtnH);
    int browseW = std::max(S(120), Button::IdealWidth(browseButton_));
    int rowY = rcFolder_.top + S(kHeader);
    rcPathFrame_ = {rcFolder_.left + pad, rowY, rcFolder_.right - pad - browseW - S(8), rowY + S(34)};
    {
        HDC hdc = GetDC(hwnd_);
        HGDIOBJ old = SelectObject(hdc, g_fonts.ui);
        TEXTMETRICW tm{};
        GetTextMetricsW(hdc, &tm);
        SelectObject(hdc, old);
        ReleaseDC(hwnd_, hdc);
        int editH = tm.tmHeight + 2;
        place(pathEdit_, rcPathFrame_.left + S(10), rcPathFrame_.top + (S(34) - editH) / 2,
              rcPathFrame_.right - rcPathFrame_.left - S(20), editH);
    }
    place(browseButton_, rcFolder_.right - pad - browseW, rowY, browseW, S(34));
    rcSummary_ = {rcFolder_.left + pad, rowY + S(34) + S(8), rcFolder_.right - pad, rowY + S(34) + S(8) + S(20)};

    // Files pane.
    int colW = Button::IdealWidth(columnsButton_), refW = Button::IdealWidth(refreshButton_);
    place(columnsButton_, rcFiles_.right - pad + S(6) - colW, headerY(rcFiles_), colW, headerBtnH);
    place(refreshButton_, rcFiles_.right - pad + S(6) - colW - S(4) - refW, headerY(rcFiles_), refW, headerBtnH);
    place(list_.Hwnd(), rcFiles_.left + 1, rcFiles_.top + S(kHeader) + 1, rcFiles_.right - rcFiles_.left - 2,
          rcFiles_.bottom - rcFiles_.top - S(kHeader) - 2);

    // Preview pane.
    int openW = Button::IdealWidth(openButton_);
    place(openButton_, rcPreview_.right - pad + S(6) - openW, headerY(rcPreview_), openW, headerBtnH);
    place(preview_.Hwnd(), rcPreview_.left + 1, rcPreview_.top + S(kHeader) + 1, rcPreview_.right - rcPreview_.left - 2,
          rcPreview_.bottom - rcPreview_.top - S(kHeader) - 2);

    // Destinations pane: tile grid + action row.
    int cols = DestinationColumns(rcDest_.right - rcDest_.left);
    int inner = rcDest_.right - rcDest_.left - pad * 2;
    int tileW = (inner - (cols - 1) * S(kTileGap)) / cols;
    int gridTop = rcDest_.top + S(kHeader);
    for (int i = 0; i < kMaxDestinations; ++i) {
        int r = i / cols, c = i % cols;
        place(destButtons_[i], rcDest_.left + pad + c * (tileW + S(kTileGap)), gridTop + r * (S(kTileHeight) + S(kTileGap)),
              tileW, S(kTileHeight));
    }
    int actionY = rcDest_.bottom - pad - S(kActionHeight);
    int delW = std::max(S(112), Button::IdealWidth(deleteButton_));
    place(deleteButton_, rcDest_.left + pad, actionY, delW, S(kActionHeight));
    int confW = Button::IdealWidth(confirmButton_);
    place(confirmButton_, rcDest_.left + pad + delW + S(14), actionY, confW, S(kActionHeight));
    int skipW = std::max(S(96), Button::IdealWidth(skipButton_));
    int undoW = std::max(S(96), Button::IdealWidth(undoButton_));
    place(skipButton_, rcDest_.right - pad - skipW, actionY, skipW, S(kActionHeight));
    place(undoButton_, rcDest_.right - pad - skipW - S(8) - undoW, actionY, undoW, S(kActionHeight));
    // Hide the confirm toggle if the pane is too narrow for it.
    bool confirmFits = rcDest_.left + pad + delW + S(14) + confW + S(12) < rcDest_.right - pad - skipW - S(8) - undoW;
    ShowWindow(confirmButton_, confirmFits ? SW_SHOWNA : SW_HIDE);
    EndDeferWindowPos(dwp);

    destHintRight_ = rcDest_.right - pad;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

App::Splitter App::HitSplitter(POINT pt) const {
    int g = S(kGap);
    RECT v = {rcFolder_.right, rcArea_.top, rcPreview_.left, rcArea_.bottom};
    RECT l = {rcFolder_.left, rcFolder_.bottom, rcFolder_.right, rcFiles_.top};
    RECT r = {rcPreview_.left, rcPreview_.bottom, rcPreview_.right, rcDest_.top};
    InflateRect(&v, g / 4, 0);
    InflateRect(&l, 0, g / 4);
    InflateRect(&r, 0, g / 4);
    if (PtInRect(&v, pt)) return Splitter::Vertical;
    if (PtInRect(&l, pt)) return Splitter::Left;
    if (PtInRect(&r, pt)) return Splitter::Right;
    return Splitter::None;
}

// =============================================================================================
// Painting

void App::DrawPaneTitle(HDC hdc, const RECT& pane, wchar_t glyph, const wchar_t* title, int rightLimit) {
    const Palette& p = Theme::Colors();
    int x = pane.left + S(kPad);
    RECT gr = {x, pane.top + S(2), x + S(20), pane.top + S(kHeader)};
    HGDIOBJ old = SelectObject(hdc, g_fonts.icon);
    SetTextColor(hdc, p.accent);
    DrawTextW(hdc, &glyph, 1, &gr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
    RECT tr = {x + S(26), pane.top + S(2), rightLimit, pane.top + S(kHeader)};
    SelectObject(hdc, g_fonts.uiBold);
    SetTextColor(hdc, p.text);
    DrawTextW(hdc, title, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(hdc, old);
}

void App::Paint(HDC target) {
    RECT client;
    GetClientRect(hwnd_, &client);
    if (client.right <= 0 || client.bottom <= 0) return;
    HDC hdc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, client.right, client.bottom);
    HGDIOBJ oldBmp = SelectObject(hdc, bmp);
    const Palette& p = Theme::Colors();
    Theme::FillRectColor(hdc, client, p.window);
    SetBkMode(hdc, TRANSPARENT);

    for (const RECT* pane : {&rcFolder_, &rcFiles_, &rcPreview_, &rcDest_}) {
        Theme::FillRectColor(hdc, *pane, p.surface);
        Theme::FrameRectColor(hdc, *pane, p.border);
    }
    // Dividers under the headers of the two panes whose content runs edge to edge.
    for (const RECT* pane : {&rcFiles_, &rcPreview_}) {
        RECT line = {pane->left + 1, pane->top + S(kHeader), pane->right - 1, pane->top + S(kHeader) + 1};
        Theme::FillRectColor(hdc, line, p.border);
    }

    RECT themeRc{}, refreshRc{}, openRc{};
    GetWindowRect(themeButton_, &themeRc);
    GetWindowRect(refreshButton_, &refreshRc);
    GetWindowRect(openButton_, &openRc);
    MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&themeRc), 2);
    MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&refreshRc), 2);
    MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&openRc), 2);

    DrawPaneTitle(hdc, rcFolder_, Glyph::Folder, L"Folder to sort", themeRc.left - S(8));
    std::wstring filesTitle = L"Files";
    if (!list_.Folder().empty()) filesTitle += L"  (" + FormatNumber(static_cast<unsigned long long>(list_.Count())) + L")";
    DrawPaneTitle(hdc, rcFiles_, Glyph::Document, filesTitle.c_str(), refreshRc.left - S(8));
    DrawPaneTitle(hdc, rcPreview_, Glyph::Preview, L"Preview", openRc.left - S(8));
    DrawPaneTitle(hdc, rcDest_, 0xE8DE /*MoveToFolder*/, L"Sort to", rcDest_.left + S(kPad) + S(120));

    // Keyboard hint on the right of the destinations header.
    {
        RECT hr = {rcDest_.left + S(kPad) + S(110), rcDest_.top + S(2), destHintRight_, rcDest_.top + S(kHeader)};
        HGDIOBJ old = SelectObject(hdc, g_fonts.small);
        SetTextColor(hdc, p.textMuted);
        DrawTextW(hdc, L"Keys: 1–5 move  ·  Del delete  ·  Space skip  ·  Ctrl+Z undo", -1, &hr,
                  DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(hdc, old);
    }

    Theme::DrawInputFrame(hdc, rcPathFrame_, GetFocus() == pathEdit_, dpi_);
    {
        HGDIOBJ old = SelectObject(hdc, g_fonts.small);
        SetTextColor(hdc, p.textMuted);
        DrawTextW(hdc, summary_.c_str(), -1, &rcSummary_, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(hdc, old);
    }

    // Status bar.
    {
        int g = S(kGap) + S(4);
        std::wstring position;
        int sel = list_.Selected();
        if (list_.Count() > 0 && sel >= 0)
            position = L"File " + FormatNumber(static_cast<unsigned long long>(sel + 1)) + L" of " +
                       FormatNumber(static_cast<unsigned long long>(list_.Count()));
        if (handledCount_ > 0)
            position = FormatNumber(static_cast<unsigned long long>(handledCount_)) + L" sorted this session" +
                       (position.empty() ? L"" : L"  ·  " + position);
        HGDIOBJ old = SelectObject(hdc, g_fonts.small);
        SIZE posSize{};
        GetTextExtentPoint32W(hdc, position.c_str(), static_cast<int>(position.size()), &posSize);
        RECT right = {rcStatus_.right - g - posSize.cx, rcStatus_.top, rcStatus_.right - g, rcStatus_.bottom - S(2)};
        SetTextColor(hdc, p.textMuted);
        DrawTextW(hdc, position.c_str(), -1, &right, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
        RECT left = {rcStatus_.left + g, rcStatus_.top, right.left - S(16), rcStatus_.bottom - S(2)};
        SetTextColor(hdc, p.text);
        DrawTextW(hdc, status_.c_str(), -1, &left, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(hdc, old);
    }

    BitBlt(target, 0, 0, client.right, client.bottom, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(hdc);
}

// =============================================================================================
// Theme

void App::ApplyTheme(bool dark) {
    Theme::SetDark(dark);
    Theme::ApplyTitleBar(hwnd_);
    list_.ApplyTheme();
    preview_.OnThemeChanged();
    Button::RefreshTooltipThemes();
    Button::SetChecked(themeButton_, dark);
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

void App::ToggleTheme() {
    bool dark = !Theme::IsDark();
    settings_.theme = dark ? ThemeMode::Dark : ThemeMode::Light;
    ApplyTheme(dark);
    settings_.Save();
}

// =============================================================================================
// Folder & selection

void App::BrowseForFolder() {
    if (busy_) return;
    std::wstring start = list_.Folder().empty() ? settings_.lastFolder : list_.Folder();
    std::wstring folder = PickFolder(hwnd_, L"Choose the folder to sort", start);
    if (!folder.empty()) LoadFolder(folder);
    SetFocus(list_.Hwnd());
}

bool App::LoadFolder(const std::wstring& folderIn, const std::wstring& selectName) {
    if (busy_) return false;
    std::wstring folder = NormalizeDir(Trim(folderIn));
    wchar_t full[MAX_PATH * 4] = {};
    if (!folder.empty() && GetFullPathNameW(folder.c_str(), MAX_PATH * 4, full, nullptr)) folder = NormalizeDir(full);
    if (folder.empty() || !DirectoryExists(folder)) {
        MessageBoxW(hwnd_, (Quote(folderIn) + L" isn't a folder that can be opened.").c_str(), kAppName, MB_OK | MB_ICONWARNING);
        SetWindowTextW(pathEdit_, list_.Folder().c_str());
        return false;
    }
    bool sameFolder = SamePath(folder, list_.Folder());
    preview_.ReleaseFile(L"");
    DWORD error = 0;
    if (!list_.Load(folder, &error)) {
        MessageBoxW(hwnd_, (L"The folder couldn't be opened.\n\n" + ErrorText(error)).c_str(), kAppName, MB_OK | MB_ICONERROR);
        UpdatePreview();
        return false;
    }
    if (!sameFolder) handledCount_ = 0;
    SetWindowTextW(pathEdit_, folder.c_str());
    settings_.lastFolder = folder;
    std::wstring leaf = FileNameOf(folder);
    SetWindowTextW(hwnd_, ((leaf.empty() ? folder : leaf) + L" — " + kAppName).c_str());
    UpdateSummary();

    int index = selectName.empty() ? 0 : list_.IndexOfName(selectName);
    if (index < 0) index = 0;
    SelectIndex(list_.Count() ? index : -1);
    if (!sameFolder)
        SetStatus(list_.Count() ? L"Loaded " + FormatNumber(static_cast<unsigned long long>(list_.Count())) +
                                      (list_.Count() == 1 ? L" file" : L" files") + L" from " + Quote(leaf.empty() ? folder : leaf) + L"."
                                : L"This folder has no files.");
    return true;
}

void App::Refresh() {
    if (list_.Folder().empty() || busy_) return;
    int sel = list_.Selected();
    std::wstring name = sel >= 0 ? list_.At(sel)->name : L"";
    if (LoadFolder(list_.Folder(), name)) SetStatus(L"Folder refreshed.");
}

void App::SelectIndex(int index) {
    list_.Select(index);
    UpdatePreview();
}

void App::UpdatePreview() {
    int index = list_.Selected();
    if (index >= 0) {
        preview_.ShowFile(list_.PathOf(index), *list_.At(index));
    } else if (list_.Folder().empty()) {
        preview_.ShowPlaceholder(Glyph::FolderOpen, L"Nothing to preview yet",
                                 L"Choose a folder to sort. Its files will be listed on the left, and the selected file is previewed here.");
    } else if (list_.Count() == 0) {
        if (handledCount_ > 0)
            preview_.ShowPlaceholder(Glyph::Completed, L"All done!", L"Every file in this folder has been sorted.");
        else
            preview_.ShowPlaceholder(Glyph::FolderOpen, L"This folder has no files",
                                     L"Choose another folder, or add files and press F5 to refresh.");
    } else {
        preview_.ShowPlaceholder(Glyph::Preview, L"Select a file", L"Pick a file on the left to preview it.");
    }
    UpdateActionStates();
    InvalidateRect(hwnd_, &rcStatus_, FALSE);
}

void App::UpdateActionStates() {
    bool hasSelection = list_.Selected() >= 0;
    EnableWindow(deleteButton_, hasSelection && !busy_);
    EnableWindow(skipButton_, hasSelection && !busy_);
    EnableWindow(openButton_, hasSelection);
    EnableWindow(undoButton_, !undo_.empty() && !busy_);
    EnableWindow(refreshButton_, !list_.Folder().empty() && !busy_);
    EnableWindow(browseButton_, !busy_);
}

void App::UpdateSummary() {
    if (list_.Folder().empty()) {
        summary_ = L"No folder selected. Click Browse, or drop a folder onto this window.";
    } else {
        int count = list_.Count();
        summary_ = FormatNumber(static_cast<unsigned long long>(count)) + (count == 1 ? L" file" : L" files") +
                   L"  ·  " + FormatBytes(list_.TotalSize());
        int hidden = list_.HiddenCount();
        if (hidden) summary_ += L"  ·  " + FormatNumber(static_cast<unsigned long long>(hidden)) + L" hidden or system";
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::UpdateDestinationButtons() {
    for (int i = 0; i < kMaxDestinations; ++i) {
        const Destination& d = settings_.destinations[i];
        HWND b = destButtons_[i];
        Button::SetEmpty(b, !d.IsSet());
        if (d.IsSet()) {
            Button::SetText(b, d.name.empty() ? FileNameOf(d.path) : d.name);
            Button::SetSubText(b, d.path);
            Button::SetTooltip(b, L"Move the selected file to " + d.path + L"  (key " + std::to_wstring(i + 1) +
                                      L")\nClick the pencil or right-click to edit.");
        } else {
            Button::SetText(b, L"Add destination");
            Button::SetSubText(b, L"");
            Button::SetTooltip(b, L"Set up a folder for button " + std::to_wstring(i + 1));
        }
    }
}

void App::SetStatus(const std::wstring& text) {
    status_ = text;
    statusBase_ = text;
    InvalidateRect(hwnd_, &rcStatus_, FALSE);
}

// =============================================================================================
// Actions

void App::SendToDestination(int slot) {
    if (busy_ || slot < 0 || slot >= kMaxDestinations) return;
    const Destination& dest = settings_.destinations[slot];
    if (!dest.IsSet()) {
        ConfigureDestination(slot);
        return;
    }
    int index = list_.Selected();
    if (index < 0) {
        SetStatus(list_.Count() ? L"Select a file first." : L"There are no files to sort.");
        return;
    }
    if (SamePath(dest.path, list_.Folder())) {
        MessageBoxW(hwnd_, (Quote(dest.name) + L" points at the folder you are sorting, so there is nowhere to move the file.").c_str(),
                    kAppName, MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!DirectoryExists(dest.path)) {
        std::wstring msg = L"The folder for " + Quote(dest.name) + L" doesn't exist:\n" + dest.path + L"\n\nCreate it now?";
        if (MessageBoxW(hwnd_, msg.c_str(), kAppName, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        int rc = SHCreateDirectoryExW(hwnd_, dest.path.c_str(), nullptr);
        if (rc != ERROR_SUCCESS && rc != ERROR_ALREADY_EXISTS) {
            MessageBoxW(hwnd_, (L"The folder couldn't be created.\n\n" + ErrorText(static_cast<DWORD>(rc))).c_str(), kAppName,
                        MB_OK | MB_ICONERROR);
            return;
        }
    }
    const FileEntry* entry = list_.At(index);
    std::wstring target = UniqueTarget(dest.path, entry->name);
    if (target.empty()) {
        SetStatus(L"Couldn't find a free file name in " + dest.path + L".");
        return;
    }
    auto* op = new FileOp;
    op->kind = FileOp::Kind::Sort;
    op->source = list_.PathOf(index);
    op->target = target;
    op->label = dest.name;
    StartOperation(op);
}

void App::StartOperation(FileOp* op) {
    busy_ = true;
    op->notify = hwnd_;
    UpdateActionStates();
    preview_.ReleaseFile(op->kind == FileOp::Kind::Sort ? L"Moving to " + Quote(op->label) + L"…" : L"Restoring…");
    SetStatus((op->kind == FileOp::Kind::Sort ? L"Moving " : L"Restoring ") + Quote(FileNameOf(op->source)) + L"…");
    HANDLE thread = CreateThread(nullptr, 0, MoveThread, op, 0, nullptr);
    if (thread)
        CloseHandle(thread);
    else
        MoveThread(op);  // posts WM_APP_OPDONE itself
}

void App::OnOperationDone(FileOp* op) {
    busy_ = false;
    std::wstring name = FileNameOf(op->source);
    if (op->error) {
        std::wstring reason = ErrorText(op->error);
        if (op->error == ERROR_SHARING_VIOLATION || op->error == ERROR_LOCK_VIOLATION)
            reason = L"The file is open in another program. Close it and try again.";
        SetStatus(L"Couldn't move " + Quote(name) + L": " + reason);
        MessageBoxW(hwnd_, (L"Couldn't move " + Quote(name) + L".\n\n" + reason).c_str(), kAppName, MB_OK | MB_ICONWARNING);
        if (op->kind == FileOp::Kind::Undo) undo_.push_back({op->target, op->source, op->label});
        UpdatePreview();
    } else if (op->kind == FileOp::Kind::Sort) {
        int index = list_.IndexOfName(name);
        if (index < 0) index = list_.Selected();
        if (index >= 0 && CompareStringOrdinal(list_.At(index)->name.c_str(), -1, name.c_str(), -1, TRUE) == CSTR_EQUAL)
            list_.RemoveAt(index);
        undo_.push_back({op->source, op->target, op->label});
        if (undo_.size() > 100) undo_.erase(undo_.begin());
        ++handledCount_;
        std::wstring msg = L"Moved " + Quote(name) + L" to " + op->label;
        std::wstring newName = FileNameOf(op->target);
        if (newName != name) msg += L" as " + Quote(newName) + L" (a file with that name was already there)";
        msg += L".";
        if (op->sourceLeftBehind) msg += L" Warning: the original couldn't be removed and is still in this folder.";
        SetStatus(msg);
        UpdateSummary();
        SelectIndex(std::min(std::max(index, 0), list_.Count() - 1));
    } else {
        --handledCount_;
        if (handledCount_ < 0) handledCount_ = 0;
        std::wstring restoredDir = ParentDir(op->target);
        int select = list_.Selected();
        if (!list_.Folder().empty() && SamePath(restoredDir, list_.Folder())) {
            FileEntry entry;
            if (FileList::ReadEntry(op->target, entry)) select = list_.Insert(entry);
        }
        SetStatus(L"Undone: " + Quote(FileNameOf(op->target)) + L" is back in " + restoredDir + L".");
        UpdateSummary();
        preview_.ReleaseFile(L"");
        SelectIndex(select);
    }
    delete op;
    UpdateActionStates();
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (closePending_) {
        SaveSettings();
        DestroyWindow(hwnd_);
    }
}

void App::DeleteSelected(bool permanent) {
    if (busy_) return;
    int index = list_.Selected();
    if (index < 0) return;
    std::wstring name = list_.At(index)->name;
    std::wstring path = list_.PathOf(index);

    if (permanent) {
        std::wstring msg = L"Permanently delete " + Quote(name) + L"?\n\nIt will not go to the Recycle Bin and can't be recovered.";
        if (MessageBoxW(hwnd_, msg.c_str(), L"Delete permanently", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
    } else if (settings_.confirmDelete) {
        std::wstring msg = L"Move " + Quote(name) + L" to the Recycle Bin?";
        if (MessageBoxW(hwnd_, msg.c_str(), L"Delete file", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    }

    preview_.ReleaseFile(L"Deleting…");
    HCURSOR oldCursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    DWORD error = 0;
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (permanent) {
            DWORD attrs = GetFileAttributesW(path.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY))
                SetFileAttributesW(path.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
            error = DeleteFileW(path.c_str()) ? 0 : GetLastError();
        } else {
            std::wstring from = path;
            from.push_back(L'\0');  // SHFileOperation wants a double-NUL-terminated list
            SHFILEOPSTRUCTW op{};
            op.hwnd = hwnd_;
            op.wFunc = FO_DELETE;
            op.pFrom = from.c_str();
            op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
            int rc = SHFileOperationW(&op);
            error = rc ? static_cast<DWORD>(rc) : (op.fAnyOperationsAborted ? ERROR_CANCELLED : 0);
            if (!error && PathExists(path)) error = ERROR_ACCESS_DENIED;
        }
        if (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION) break;
        Sleep(200);
    }
    SetCursor(oldCursor);

    if (error) {
        std::wstring reason = error == ERROR_SHARING_VIOLATION ? L"The file is open in another program." : ErrorText(error);
        SetStatus(L"Couldn't delete " + Quote(name) + L": " + reason);
        MessageBoxW(hwnd_, (L"Couldn't delete " + Quote(name) + L".\n\n" + reason).c_str(), kAppName, MB_OK | MB_ICONWARNING);
        UpdatePreview();
        return;
    }
    list_.RemoveAt(index);
    ++handledCount_;
    SetStatus(permanent ? L"Permanently deleted " + Quote(name) + L"."
                        : L"Moved " + Quote(name) + L" to the Recycle Bin. Restore it from there if needed.");
    UpdateSummary();
    SelectIndex(std::min(index, list_.Count() - 1));
}

void App::Skip() {
    if (busy_ || list_.Count() == 0) return;
    int index = list_.Selected();
    if (index < 0) {
        SelectIndex(0);
    } else if (index + 1 < list_.Count()) {
        SelectIndex(index + 1);
    } else {
        SetStatus(L"That's the last file in the list.");
    }
}

void App::Undo() {
    if (busy_) return;
    if (undo_.empty()) {
        SetStatus(L"Nothing to undo.");
        return;
    }
    UndoEntry u = undo_.back();
    undo_.pop_back();
    if (!PathExists(u.movedPath)) {
        SetStatus(L"Can't undo: " + Quote(FileNameOf(u.movedPath)) + L" is no longer in " + Quote(u.label) + L".");
        UpdateActionStates();
        return;
    }
    std::wstring originalDir = ParentDir(u.originalPath);
    if (!DirectoryExists(originalDir)) {
        SetStatus(L"Can't undo: the original folder " + originalDir + L" no longer exists.");
        UpdateActionStates();
        return;
    }
    auto* op = new FileOp;
    op->kind = FileOp::Kind::Undo;
    op->source = u.movedPath;
    op->target = PathExists(u.originalPath) ? UniqueTarget(originalDir, FileNameOf(u.originalPath)) : u.originalPath;
    op->label = u.label;
    StartOperation(op);
}

void App::OpenSelected() {
    int index = list_.Selected();
    if (index < 0) return;
    std::wstring path = list_.PathOf(index);
    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.fMask = SEE_MASK_INVOKEIDLIST | SEE_MASK_FLAG_NO_UI;
    sei.hwnd = hwnd_;
    sei.lpFile = path.c_str();
    sei.lpDirectory = list_.Folder().c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) {
        OPENASINFO info{};
        info.pcszFile = path.c_str();
        info.oaifInFlags = OAIF_ALLOW_REGISTRATION | OAIF_EXEC;
        SHOpenWithDialog(hwnd_, &info);
    }
}

void App::ConfigureDestination(int slot) {
    DestinationDialogResult r = ShowDestinationDialog(hwnd_, slot, settings_.destinations[slot], list_.Folder());
    if (r.action == DestinationDialogAction::Save) {
        settings_.destinations[slot] = r.value;
        SetStatus(L"Button " + std::to_wstring(slot + 1) + L" now moves files to " + r.value.path + L".");
    } else if (r.action == DestinationDialogAction::Remove) {
        settings_.destinations[slot] = {};
        SetStatus(L"Destination " + std::to_wstring(slot + 1) + L" removed.");
    } else {
        SetFocus(list_.Hwnd());
        return;
    }
    settings_.Save();
    UpdateDestinationButtons();
    SetFocus(list_.Hwnd());
}

void App::ShowDestinationMenu(int slot) {
    const Destination& d = settings_.destinations[slot];
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_MENU_EDIT, d.IsSet() ? L"Edit destination…" : L"Set up destination…");
    AppendMenuW(menu, MF_STRING | (d.IsSet() ? 0 : MF_GRAYED), ID_MENU_OPEN_FOLDER, L"Open folder in File Explorer");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (d.IsSet() ? 0 : MF_GRAYED), ID_MENU_REMOVE, L"Remove destination");
    POINT pt;
    GetCursorPos(&pt);
    int cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, hwnd_, nullptr);
    DestroyMenu(menu);
    switch (cmd) {
    case ID_MENU_EDIT:
        ConfigureDestination(slot);
        break;
    case ID_MENU_OPEN_FOLDER:
        ShellExecuteW(hwnd_, L"open", d.path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case ID_MENU_REMOVE:
        settings_.destinations[slot] = {};
        settings_.Save();
        UpdateDestinationButtons();
        SetStatus(L"Destination " + std::to_wstring(slot + 1) + L" removed.");
        break;
    }
}

void App::ShowColumnsMenu() {
    HMENU menu = CreatePopupMenu();
    for (int c = 0; c < COL_COUNT; ++c) {
        auto id = static_cast<ColumnId>(c);
        UINT flags = MF_STRING | (list_.IsColumnVisible(id) ? MF_CHECKED : MF_UNCHECKED) |
                     (FileList::ColumnRequired(id) ? MF_GRAYED : 0);
        AppendMenuW(menu, flags, static_cast<UINT_PTR>(ID_COLUMN0) + c, FileList::ColumnTitle(id));
    }
    RECT r;
    GetWindowRect(columnsButton_, &r);
    int cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, r.right, r.bottom + S(2), hwnd_, nullptr);
    DestroyMenu(menu);
    if (cmd >= ID_COLUMN0 && cmd < ID_COLUMN0 + static_cast<int>(COL_COUNT)) {
        list_.ToggleColumn(static_cast<ColumnId>(cmd - ID_COLUMN0));
        SaveSettings();
    }
}

void App::SaveSettings() {
    WINDOWPLACEMENT wp{sizeof(wp)};
    if (GetWindowPlacement(hwnd_, &wp)) {
        settings_.windowRect = wp.rcNormalPosition;
        settings_.maximized = wp.showCmd == SW_SHOWMAXIMIZED;
    }
    settings_.columnMask = list_.ColumnMask();
    settings_.columnWidths = list_.LogicalColumnWidths();
    settings_.sortColumn = list_.SortColumn();
    settings_.sortAscending = list_.SortAscending();
    settings_.confirmDelete = Button::GetChecked(confirmButton_);
    settings_.Save();
}

// =============================================================================================
// Messages

void App::OnCommand(int id, int code) {
    if (id >= IDC_DEST0 && id < IDC_DEST0 + kMaxDestinations) {
        int slot = id - IDC_DEST0;
        if (code == FSBN_EDIT)
            ConfigureDestination(slot);
        else if (code == FSBN_CONTEXT)
            ShowDestinationMenu(slot);
        else
            SendToDestination(slot);
        return;
    }
    if (id >= ID_KEY_DEST0 && id < ID_KEY_DEST0 + kMaxDestinations) {
        SendToDestination(id - ID_KEY_DEST0);
        return;
    }
    switch (id) {
    case IDC_PATH:
        if (code == EN_SETFOCUS || code == EN_KILLFOCUS) InvalidateRect(hwnd_, &rcPathFrame_, FALSE);
        break;
    case IDC_PATH_GO:
        LoadFolder(GetWindowTextString(pathEdit_));
        SetFocus(list_.Hwnd());
        break;
    case IDC_BROWSE:
    case ID_KEY_BROWSE:
        BrowseForFolder();
        break;
    case IDC_THEME:
    case ID_KEY_THEME:
        ToggleTheme();
        break;
    case IDC_REFRESH:
    case ID_KEY_REFRESH:
        Refresh();
        break;
    case IDC_COLUMNS:
        ShowColumnsMenu();
        break;
    case IDC_OPEN:
    case ID_KEY_OPEN:
        OpenSelected();
        break;
    case IDC_DELETE:
        DeleteSelected(GetKeyState(VK_SHIFT) < 0);
        break;
    case ID_KEY_DELETE:
        DeleteSelected(false);
        break;
    case ID_KEY_DELETE_PERMANENT:
        DeleteSelected(true);
        break;
    case IDC_SKIP:
    case ID_KEY_SKIP:
        Skip();
        break;
    case IDC_UNDO:
    case ID_KEY_UNDO:
        Undo();
        break;
    case IDC_CONFIRM:
        settings_.confirmDelete = Button::GetChecked(confirmButton_);
        settings_.Save();
        break;
    }
}

LRESULT App::OnNotify(NMHDR* hdr) {
    LRESULT result = 0;
    if (list_.HandleNotify(hdr, &result)) return result;
    if (hdr->hwndFrom == list_.Hwnd()) {
        switch (hdr->code) {
        case LVN_ITEMCHANGED: {
            auto* nm = reinterpret_cast<NMLISTVIEW*>(hdr);
            if ((nm->uChanged & LVIF_STATE) && ((nm->uNewState ^ nm->uOldState) & LVIS_SELECTED)) UpdatePreview();
            break;
        }
        case LVN_ITEMACTIVATE:
            OpenSelected();
            break;
        }
    }
    return 0;
}

LRESULT CALLBACK App::PathEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    auto* self = reinterpret_cast<App*>(ref);
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        PostMessageW(self->hwnd_, WM_COMMAND, MAKEWPARAM(IDC_PATH_GO, 0), 0);
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        SetWindowTextW(hwnd, self->list_.Folder().c_str());
        SetFocus(self->list_.Hwnd());
        return 0;
    }
    if (msg == WM_CHAR && (wp == L'\r' || wp == 27)) return 0;  // no beep
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT App::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        OnCreate();
        return 0;
    case WM_SIZE:
        Layout();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd_, &ps);
        Paint(hdc);
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize = {S(900), S(600)};
        return 0;
    }
    case WM_DPICHANGED: {
        UINT oldDpi = dpi_;
        dpi_ = HIWORD(wp);
        g_fonts.Create(dpi_);
        UpdateFonts();
        list_.OnDpiChanged(oldDpi, dpi_);
        auto* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Layout();
        RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
        return 0;
    }
    case WM_SETTINGCHANGE:
        if (settings_.theme == ThemeMode::System && lp && lstrcmpiW(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0)
            ApplyTheme(Theme::SystemPrefersDark());
        break;
    case WM_COMMAND:
        OnCommand(LOWORD(wp), HIWORD(wp));
        return 0;
    case WM_NOTIFY:
        return OnNotify(reinterpret_cast<NMHDR*>(lp));
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        const Palette& p = Theme::Colors();
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetTextColor(hdc, p.text);
        SetBkColor(hdc, p.input);
        return reinterpret_cast<LRESULT>(Theme::Brush(p.input));
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && reinterpret_cast<HWND>(wp) == hwnd_) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd_, &pt);
            Splitter s = HitSplitter(pt);
            if (s != Splitter::None) {
                SetCursor(LoadCursorW(nullptr, s == Splitter::Vertical ? IDC_SIZEWE : IDC_SIZENS));
                return TRUE;
            }
        }
        break;
    case WM_LBUTTONDOWN: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        dragging_ = HitSplitter(pt);
        if (dragging_ != Splitter::None) {
            SetCapture(hwnd_);
            if (dragging_ == Splitter::Vertical) dragOffset_ = pt.x - rcFolder_.right;
            else if (dragging_ == Splitter::Left) dragOffset_ = pt.y - rcFolder_.bottom;
            else dragOffset_ = pt.y - rcDest_.top;
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (dragging_ != Splitter::None) {
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int g = S(kGap);
            if (dragging_ == Splitter::Vertical) {
                int usable = rcArea_.right - rcArea_.left - g;
                if (usable > 0)
                    settings_.mainSplit = std::clamp(static_cast<double>(pt.x - dragOffset_ - rcArea_.left) / usable, 0.15, 0.85);
            } else if (dragging_ == Splitter::Left) {
                settings_.leftTopHeight = std::max(1, MulDiv(pt.y - dragOffset_ - rcArea_.top, 96, static_cast<int>(dpi_)));
            } else {
                settings_.destHeight = std::max(1, MulDiv(rcArea_.bottom - (pt.y - dragOffset_), 96, static_cast<int>(dpi_)));
            }
            Layout();
            UpdateWindow(hwnd_);
        }
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (dragging_ != Splitter::None) {
            dragging_ = Splitter::None;
            if (msg == WM_LBUTTONUP) ReleaseCapture();
        }
        return 0;
    case WM_LBUTTONDBLCLK:
        break;
    case WM_DROPFILES: {
        auto drop = reinterpret_cast<HDROP>(wp);
        wchar_t path[MAX_PATH * 4] = {};
        if (DragQueryFileW(drop, 0, path, MAX_PATH * 4)) {
            if (DirectoryExists(path))
                LoadFolder(path);
            else if (PathExists(path))
                LoadFolder(ParentDir(path), FileNameOf(path));
        }
        DragFinish(drop);
        SetForegroundWindow(hwnd_);
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) {
            lastFocus_ = GetFocus();
        } else if (!HIWORD(wp)) {
            HWND target = lastFocus_ && IsWindow(lastFocus_) && IsChild(hwnd_, lastFocus_) ? lastFocus_ : list_.Hwnd();
            PostMessageW(hwnd_, WM_APP_RESTOREFOCUS, reinterpret_cast<WPARAM>(target), 0);
        }
        return 0;
    case WM_SETFOCUS:
        SetFocus(list_.Hwnd());
        return 0;
    case WM_APP_RESTOREFOCUS: {
        HWND target = wp ? reinterpret_cast<HWND>(wp) : list_.Hwnd();
        if (GetForegroundWindow() == hwnd_ && IsWindow(target)) SetFocus(target);
        return 0;
    }
    case WM_APP_OPPROGRESS:
        if (busy_) {
            status_ = statusBase_ + L" " + std::to_wstring(wp) + L"%";
            InvalidateRect(hwnd_, &rcStatus_, FALSE);
        }
        return 0;
    case WM_APP_OPDONE:
        OnOperationDone(reinterpret_cast<FileOp*>(lp));
        return 0;
    case WM_CLOSE:
        if (busy_) {
            closePending_ = true;
            SetStatus(L"Finishing the current move before closing…");
            return 0;
        }
        SaveSettings();
        DestroyWindow(hwnd_);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

int App::Run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // Single-key shortcuts (1-5, Space, Del) must not fire while the user is typing a path.
        HWND focus = GetFocus();
        bool typing = focus == pathEdit_;
        if (!typing && accelerators_ && (msg.hwnd == hwnd_ || IsChild(hwnd_, msg.hwnd)) &&
            TranslateAcceleratorW(hwnd_, accelerators_, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (accelerators_) DestroyAcceleratorTable(accelerators_);
    return static_cast<int>(msg.wParam);
}
