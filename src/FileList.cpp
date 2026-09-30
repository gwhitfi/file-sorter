#include "FileList.h"
#include "Theme.h"

namespace {

struct ColumnDef {
    const wchar_t* title;
    int width;  // logical default width
    int format;
    bool required;
    bool defaultVisible;
};

const ColumnDef kColumns[COL_COUNT] = {
    {L"Name", 230, LVCFMT_LEFT, true, true},
    {L"Type", 140, LVCFMT_LEFT, true, true},
    {L"Size", 80, LVCFMT_RIGHT, true, true},
    {L"Date modified", 140, LVCFMT_LEFT, false, true},
    {L"Date created", 150, LVCFMT_LEFT, false, false},
    {L"Date accessed", 150, LVCFMT_LEFT, false, false},
    {L"Extension", 80, LVCFMT_LEFT, false, false},
    {L"Attributes", 80, LVCFMT_LEFT, false, false},
};

unsigned DefaultMask() {
    unsigned mask = 0;
    for (int i = 0; i < COL_COUNT; ++i)
        if (kColumns[i].defaultVisible) mask |= 1u << i;
    return mask;
}

unsigned RequiredMask() {
    unsigned mask = 0;
    for (int i = 0; i < COL_COUNT; ++i)
        if (kColumns[i].required) mask |= 1u << i;
    return mask;
}

struct TypeInfo {
    std::wstring ext;
    std::wstring typeName;
    int icon;
};

// SHGetFileInfo is slow enough to matter in big folders, so cache by extension.
const TypeInfo& LookupType(const std::wstring& ext) {
    static std::vector<TypeInfo> cache;
    for (const auto& t : cache)
        if (t.ext == ext) return t;
    SHFILEINFOW sfi{};
    std::wstring probe = L"file" + ext;
    SHGetFileInfoW(probe.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                   SHGFI_TYPENAME | SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
    std::wstring type = sfi.szTypeName;
    if (type.empty()) type = ext.empty() ? L"File" : ToLower(ext.substr(1)) + L" file";
    cache.push_back({ext, type, sfi.iIcon});
    return cache.back();
}

std::wstring AttributeString(DWORD a) {
    std::wstring s;
    if (a & FILE_ATTRIBUTE_READONLY) s += L'R';
    if (a & FILE_ATTRIBUTE_HIDDEN) s += L'H';
    if (a & FILE_ATTRIBUTE_SYSTEM) s += L'S';
    if (a & FILE_ATTRIBUTE_ARCHIVE) s += L'A';
    if (a & FILE_ATTRIBUTE_COMPRESSED) s += L'C';
    if (a & FILE_ATTRIBUTE_ENCRYPTED) s += L'E';
    if (a & FILE_ATTRIBUTE_OFFLINE) s += L'O';
    return s;
}

}  // namespace

bool FileList::Create(HWND parent, int id) {
    hwnd_ = CreateWindowExW(0, WC_LISTVIEWW, L"",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL |
                                LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS,
                            0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                            GetModuleHandleW(nullptr), nullptr);
    if (!hwnd_) return false;
    ListView_SetExtendedListViewStyle(hwnd_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    SHFILEINFOW sfi{};
    auto imageList = reinterpret_cast<HIMAGELIST>(
        SHGetFileInfoW(L"file.txt", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                       SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES));
    ListView_SetImageList(hwnd_, imageList, LVSIL_SMALL);
    SetWindowSubclass(hwnd_, ListSubclass, 1, reinterpret_cast<DWORD_PTR>(this));

    columnMask_ = DefaultMask();
    for (int i = 0; i < COL_COUNT; ++i) widths_[i] = kColumns[i].width;
    RebuildColumns();
    return true;
}

void FileList::Configure(unsigned mask, const std::vector<int>& logicalWidths, int sortColumn, bool ascending) {
    columnMask_ = (mask ? mask : DefaultMask()) | RequiredMask();
    columnMask_ &= (1u << COL_COUNT) - 1;
    for (int i = 0; i < COL_COUNT; ++i) {
        int w = i < static_cast<int>(logicalWidths.size()) ? logicalWidths[i] : 0;
        widths_[i] = w >= 30 && w <= 2000 ? w : kColumns[i].width;
    }
    sortColumn_ = sortColumn >= 0 && sortColumn < COL_COUNT ? sortColumn : COL_NAME;
    if (!IsColumnVisible(static_cast<ColumnId>(sortColumn_))) sortColumn_ = COL_NAME;
    sortAscending_ = ascending;
    RebuildColumns();
    SortItems();
    ListView_RedrawItems(hwnd_, 0, Count());
}

const wchar_t* FileList::ColumnTitle(ColumnId id) { return kColumns[id].title; }
bool FileList::ColumnRequired(ColumnId id) { return kColumns[id].required; }

void FileList::CaptureWidths() {
    UINT dpi = WindowDpi(hwnd_);
    for (size_t i = 0; i < visible_.size(); ++i) {
        int px = ListView_GetColumnWidth(hwnd_, static_cast<int>(i));
        if (px > 0) widths_[visible_[i]] = MulDiv(px, 96, static_cast<int>(dpi));
    }
}

std::vector<int> FileList::LogicalColumnWidths() {
    CaptureWidths();
    return std::vector<int>(widths_, widths_ + COL_COUNT);
}

void FileList::ToggleColumn(ColumnId id) {
    if (kColumns[id].required) return;
    CaptureWidths();
    columnMask_ ^= 1u << id;
    if (!IsColumnVisible(static_cast<ColumnId>(sortColumn_))) sortColumn_ = COL_NAME;
    RebuildColumns();
    SortItems();
    ListView_RedrawItems(hwnd_, 0, Count());
}

void FileList::RebuildColumns() {
    SendMessageW(hwnd_, WM_SETREDRAW, FALSE, 0);
    HWND header = ListView_GetHeader(hwnd_);
    int existing = header ? Header_GetItemCount(header) : 0;
    for (int i = existing - 1; i >= 0; --i) ListView_DeleteColumn(hwnd_, i);

    visible_.clear();
    UINT dpi = WindowDpi(hwnd_);
    for (int i = 0; i < COL_COUNT; ++i) {
        if (!IsColumnVisible(static_cast<ColumnId>(i))) continue;
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.pszText = const_cast<wchar_t*>(kColumns[i].title);
        col.cx = ScaleDpi(widths_[i], dpi);
        col.fmt = kColumns[i].format;
        ListView_InsertColumn(hwnd_, static_cast<int>(visible_.size()), &col);
        visible_.push_back(static_cast<ColumnId>(i));
    }
    UpdateSortArrow();
    SendMessageW(hwnd_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void FileList::UpdateSortArrow() {
    HWND header = ListView_GetHeader(hwnd_);
    for (size_t i = 0; i < visible_.size(); ++i) {
        HDITEMW item{};
        item.mask = HDI_FORMAT;
        Header_GetItem(header, static_cast<int>(i), &item);
        item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (visible_[i] == sortColumn_) item.fmt |= sortAscending_ ? HDF_SORTUP : HDF_SORTDOWN;
        Header_SetItem(header, static_cast<int>(i), &item);
    }
}

FileEntry FileList::MakeEntry(const WIN32_FIND_DATAW& fd) {
    FileEntry e;
    e.name = fd.cFileName;
    const wchar_t* dot = PathFindExtensionW(fd.cFileName);
    e.ext = (dot && *dot) ? ToLower(dot) : L"";
    e.size = (static_cast<unsigned long long>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
    e.modified = fd.ftLastWriteTime;
    e.created = fd.ftCreationTime;
    e.accessed = fd.ftLastAccessTime;
    e.attributes = fd.dwFileAttributes;
    const TypeInfo& t = LookupType(e.ext);
    e.typeName = t.typeName;
    e.icon = t.icon;
    return e;
}

bool FileList::ReadEntry(const std::wstring& path, FileEntry& out) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileExW(path.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    FindClose(h);
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
    out = MakeEntry(fd);
    return true;
}

bool FileList::Load(const std::wstring& folder, DWORD* error) {
    std::vector<FileEntry> items;
    WIN32_FIND_DATAW fd{};
    std::wstring pattern = JoinPath(folder, L"*");
    HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err != ERROR_FILE_NOT_FOUND) {
            if (error) *error = err;
            return false;
        }
    } else {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;  // files only; hidden/system are included
            items.push_back(MakeEntry(fd));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    folder_ = NormalizeDir(folder);
    items_ = std::move(items);
    SortItems();
    ListView_SetItemState(hwnd_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemCountEx(hwnd_, Count(), 0);
    InvalidateRect(hwnd_, nullptr, TRUE);
    return true;
}

void FileList::Clear() {
    folder_.clear();
    items_.clear();
    ListView_SetItemCountEx(hwnd_, 0, 0);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

const FileEntry* FileList::At(int index) const {
    return index >= 0 && index < Count() ? &items_[static_cast<size_t>(index)] : nullptr;
}

std::wstring FileList::PathOf(int index) const {
    const FileEntry* e = At(index);
    return e ? JoinPath(folder_, e->name) : L"";
}

int FileList::IndexOfName(const std::wstring& name) const {
    for (int i = 0; i < Count(); ++i)
        if (CompareStringOrdinal(items_[i].name.c_str(), -1, name.c_str(), -1, TRUE) == CSTR_EQUAL) return i;
    return -1;
}

int FileList::Selected() const {
    int index = ListView_GetNextItem(hwnd_, -1, LVNI_SELECTED);
    return index < Count() ? index : -1;
}

void FileList::Select(int index) {
    ListView_SetItemState(hwnd_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    if (index < 0 || index >= Count()) return;
    ListView_SetItemState(hwnd_, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(hwnd_, index, FALSE);
}

void FileList::RemoveAt(int index) {
    if (index < 0 || index >= Count()) return;
    items_.erase(items_.begin() + index);
    ListView_SetItemState(hwnd_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemCountEx(hwnd_, Count(), LVSICF_NOSCROLL);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

int FileList::Insert(const FileEntry& entry) {
    std::wstring name = entry.name;
    items_.push_back(entry);
    SortItems();
    ListView_SetItemCountEx(hwnd_, Count(), LVSICF_NOSCROLL);
    InvalidateRect(hwnd_, nullptr, TRUE);
    return IndexOfName(name);
}

unsigned long long FileList::TotalSize() const {
    unsigned long long total = 0;
    for (const auto& e : items_) total += e.size;
    return total;
}

int FileList::HiddenCount() const {
    return static_cast<int>(std::count_if(items_.begin(), items_.end(), [](const FileEntry& e) { return e.IsHidden(); }));
}

void FileList::SortItems() {
    int col = sortColumn_;
    bool asc = sortAscending_;
    auto byName = [](const FileEntry& a, const FileEntry& b) { return StrCmpLogicalW(a.name.c_str(), b.name.c_str()); };
    std::stable_sort(items_.begin(), items_.end(), [&](const FileEntry& a, const FileEntry& b) {
        int c = 0;
        switch (col) {
        case COL_TYPE: c = CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE, a.typeName.c_str(), -1, b.typeName.c_str(), -1, nullptr, nullptr, 0) - 2; break;
        case COL_SIZE: c = a.size < b.size ? -1 : a.size > b.size ? 1 : 0; break;
        case COL_MODIFIED: c = CompareFileTime(&a.modified, &b.modified); break;
        case COL_CREATED: c = CompareFileTime(&a.created, &b.created); break;
        case COL_ACCESSED: c = CompareFileTime(&a.accessed, &b.accessed); break;
        case COL_EXTENSION: c = a.ext.compare(b.ext); break;
        case COL_ATTRIBUTES: c = AttributeString(a.attributes).compare(AttributeString(b.attributes)); break;
        default: break;
        }
        if (c == 0) c = byName(a, b);
        return asc ? c < 0 : c > 0;
    });
    UpdateSortArrow();
}

std::wstring FileList::CellText(const FileEntry& e, ColumnId col) const {
    switch (col) {
    case COL_NAME: return e.name;
    case COL_TYPE: return e.typeName;
    case COL_SIZE: return FormatBytes(e.size);
    case COL_MODIFIED: return FormatFileTime(e.modified);
    case COL_CREATED: return FormatFileTime(e.created);
    case COL_ACCESSED: return FormatFileTime(e.accessed);
    case COL_EXTENSION: return e.ext.empty() ? L"" : e.ext.substr(1);
    case COL_ATTRIBUTES: return AttributeString(e.attributes);
    default: return L"";
    }
}

bool FileList::HandleNotify(NMHDR* hdr, LRESULT* result) {
    if (hdr->hwndFrom != hwnd_) return false;
    switch (hdr->code) {
    case LVN_GETDISPINFOW: {
        auto* di = reinterpret_cast<NMLVDISPINFOW*>(hdr);
        const FileEntry* e = At(di->item.iItem);
        if (!e) return true;
        if (di->item.mask & LVIF_TEXT) {
            int sub = di->item.iSubItem;
            ColumnId col = sub >= 0 && sub < static_cast<int>(visible_.size()) ? visible_[sub] : COL_NAME;
            textBuffer_ = CellText(*e, col);
            if (di->item.pszText && di->item.cchTextMax > 0)
                wcsncpy_s(di->item.pszText, di->item.cchTextMax, textBuffer_.c_str(), _TRUNCATE);
        }
        if (di->item.mask & LVIF_IMAGE) di->item.iImage = e->icon;
        *result = 0;
        return true;
    }
    case LVN_COLUMNCLICK: {
        auto* nm = reinterpret_cast<NMLISTVIEW*>(hdr);
        if (nm->iSubItem < 0 || nm->iSubItem >= static_cast<int>(visible_.size())) return true;
        int col = visible_[nm->iSubItem];
        int selected = Selected();
        std::wstring selectedName = selected >= 0 ? items_[selected].name : L"";
        if (col == sortColumn_) {
            sortAscending_ = !sortAscending_;
        } else {
            sortColumn_ = col;
            sortAscending_ = true;
        }
        SortItems();
        ListView_RedrawItems(hwnd_, 0, Count());
        if (!selectedName.empty()) {
            int index = IndexOfName(selectedName);
            Select(index);
        }
        *result = 0;
        return true;
    }
    case LVN_ODFINDITEMW: {
        // Type-to-find support for the virtual list.
        auto* fi = reinterpret_cast<NMLVFINDITEMW*>(hdr);
        *result = -1;
        if (!(fi->lvfi.flags & (LVFI_STRING | LVFI_PARTIAL)) || !fi->lvfi.psz) return true;
        size_t len = wcslen(fi->lvfi.psz);
        int n = Count();
        for (int k = 0; k < n; ++k) {
            int i = (fi->iStart + k) % n;
            const std::wstring& name = items_[i].name;
            if (name.size() >= len &&
                CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE, name.c_str(), static_cast<int>(len),
                                fi->lvfi.psz, static_cast<int>(len), nullptr, nullptr, 0) == CSTR_EQUAL) {
                *result = i;
                break;
            }
        }
        return true;
    }
    case NM_CUSTOMDRAW: {
        auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
        if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) {
            *result = CDRF_NOTIFYITEMDRAW;
        } else if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
            const FileEntry* e = At(static_cast<int>(cd->nmcd.dwItemSpec));
            // Hidden and system files are shown, but de-emphasised like in Explorer.
            cd->clrText = (e && e->IsHidden()) ? Theme::Colors().textFaint : Theme::Colors().text;
            *result = CDRF_NEWFONT;
        } else {
            *result = CDRF_DODEFAULT;
        }
        return true;
    }
    }
    return false;
}

LRESULT CALLBACK FileList::ListSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_NOTIFY) {
        // The header sends its notifications to the list view, not to our window: recolour its text here.
        auto* hdr = reinterpret_cast<NMHDR*>(lp);
        if (hdr->hwndFrom == ListView_GetHeader(hwnd) && hdr->code == NM_CUSTOMDRAW) {
            auto* cd = reinterpret_cast<NMCUSTOMDRAW*>(lp);
            if (cd->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (cd->dwDrawStage == CDDS_ITEMPREPAINT) {
                SetTextColor(cd->hdc, Theme::Colors().textMuted);
                return CDRF_DODEFAULT;
            }
        }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void FileList::ApplyTheme() {
    const Palette& p = Theme::Colors();
    ListView_SetBkColor(hwnd_, p.surface);
    ListView_SetTextBkColor(hwnd_, p.surface);
    ListView_SetTextColor(hwnd_, p.text);
    Theme::ApplyControl(hwnd_, L"Explorer", L"DarkMode_Explorer");
    Theme::ApplyControl(ListView_GetHeader(hwnd_), L"ItemsView", L"DarkMode_ItemsView");
    if (HWND tip = ListView_GetToolTips(hwnd_)) Theme::ApplyTooltip(tip);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void FileList::SetFont(HFONT font) {
    SendMessageW(hwnd_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(ListView_GetHeader(hwnd_), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void FileList::OnDpiChanged(UINT oldDpi, UINT newDpi) {
    for (size_t i = 0; i < visible_.size(); ++i) {
        int px = ListView_GetColumnWidth(hwnd_, static_cast<int>(i));
        if (px > 0) widths_[visible_[i]] = MulDiv(px, 96, static_cast<int>(oldDpi));
        ListView_SetColumnWidth(hwnd_, static_cast<int>(i), ScaleDpi(widths_[visible_[i]], newDpi));
    }
}
