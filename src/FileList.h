// The file list (virtual list-view) plus the folder scan that feeds it.
#pragma once
#include "Common.h"

struct FileEntry {
    std::wstring name;
    std::wstring ext;       // lower-case, including the dot ("" if none)
    std::wstring typeName;  // "JPEG image", "Text Document", ...
    unsigned long long size = 0;
    FILETIME modified{};
    FILETIME created{};
    FILETIME accessed{};
    DWORD attributes = 0;
    int icon = 0;  // index in the system small image list

    bool IsHidden() const { return (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0; }
};

enum ColumnId {
    COL_NAME,
    COL_TYPE,
    COL_SIZE,
    COL_MODIFIED,
    COL_CREATED,
    COL_ACCESSED,
    COL_EXTENSION,
    COL_ATTRIBUTES,
    COL_COUNT
};

class FileList {
public:
    bool Create(HWND parent, int id);
    HWND Hwnd() const { return hwnd_; }

    // Scans `folder` (non-recursive, including hidden and system files).
    bool Load(const std::wstring& folder, DWORD* error);
    void Clear();
    const std::wstring& Folder() const { return folder_; }

    int Count() const { return static_cast<int>(items_.size()); }
    const FileEntry* At(int index) const;
    std::wstring PathOf(int index) const;
    int IndexOfName(const std::wstring& name) const;
    int Selected() const;
    void Select(int index);
    void RemoveAt(int index);
    int Insert(const FileEntry& entry);  // returns the entry's index after sorting

    unsigned long long TotalSize() const;
    int HiddenCount() const;

    static bool ReadEntry(const std::wstring& path, FileEntry& out);

    // Columns.
    static const wchar_t* ColumnTitle(ColumnId id);
    static bool ColumnRequired(ColumnId id);
    bool IsColumnVisible(ColumnId id) const { return (columnMask_ >> id) & 1u; }
    void ToggleColumn(ColumnId id);
    unsigned ColumnMask() const { return columnMask_; }
    std::vector<int> LogicalColumnWidths();
    void Configure(unsigned mask, const std::vector<int>& logicalWidths, int sortColumn, bool ascending);
    int SortColumn() const { return sortColumn_; }
    bool SortAscending() const { return sortAscending_; }

    // Call from the parent's WM_NOTIFY; returns true when handled.
    bool HandleNotify(NMHDR* hdr, LRESULT* result);

    void ApplyTheme();
    void SetFont(HFONT font);
    void OnDpiChanged(UINT oldDpi, UINT newDpi);

private:
    static FileEntry MakeEntry(const WIN32_FIND_DATAW& fd);
    static LRESULT CALLBACK ListSubclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    void RebuildColumns();
    void CaptureWidths();
    void SortItems();
    void UpdateSortArrow();
    std::wstring CellText(const FileEntry& e, ColumnId col) const;

    HWND hwnd_ = nullptr;
    std::wstring folder_;
    std::vector<FileEntry> items_;
    std::vector<ColumnId> visible_;  // visible columns in display order
    unsigned columnMask_ = 0;
    int widths_[COL_COUNT] = {};     // logical (96-DPI) widths
    int sortColumn_ = COL_NAME;
    bool sortAscending_ = true;
    std::wstring textBuffer_;
};
