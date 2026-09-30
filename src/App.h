// Main window: layout, splitters, commands and the sort/delete/undo workflow.
#pragma once
#include "Common.h"
#include "FileList.h"
#include "PreviewPane.h"
#include "Settings.h"

struct FileOp;

class App {
public:
    bool Create(HINSTANCE instance, int showCommand, const std::wstring& initialFolder);
    int Run();

private:
    struct UndoEntry {
        std::wstring originalPath;  // where the file was before it was sorted
        std::wstring movedPath;     // where it is now
        std::wstring label;         // destination name, for messages
    };
    enum class Splitter { None, Vertical, Left, Right };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK PathEditProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

    void OnCreate();
    void OnCommand(int id, int code);
    LRESULT OnNotify(NMHDR* hdr);
    void RestoreWindowPlacement(int showCommand);
    void SaveSettings();

    // Layout & painting.
    void UpdateFonts();
    void Layout();
    int LeftTopMinHeight() const;
    int DestinationColumns(int width) const;
    int DestinationMinHeight(int width) const;
    void Paint(HDC hdc);
    void DrawPaneTitle(HDC hdc, const RECT& pane, wchar_t glyph, const wchar_t* title, int rightLimit);
    Splitter HitSplitter(POINT pt) const;
    int S(int v) const { return ScaleDpi(v, dpi_); }

    // Theme.
    void ApplyTheme(bool dark);
    void ToggleTheme();

    // Folder & selection.
    void BrowseForFolder();
    bool LoadFolder(const std::wstring& folder, const std::wstring& selectName = L"");
    void Refresh();
    void SelectIndex(int index);
    void UpdatePreview();
    void UpdateActionStates();
    void UpdateSummary();
    void UpdateDestinationButtons();
    void SetStatus(const std::wstring& text);

    // Actions.
    void SendToDestination(int slot);
    void ConfigureDestination(int slot);
    void ShowDestinationMenu(int slot);
    void DeleteSelected(bool permanent);
    void Skip();
    void Undo();
    void OpenSelected();
    void ShowColumnsMenu();
    void StartOperation(FileOp* op);
    void OnOperationDone(FileOp* op);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HACCEL accelerators_ = nullptr;
    UINT dpi_ = 96;
    Settings settings_;

    // Controls.
    HWND pathEdit_ = nullptr;
    HWND browseButton_ = nullptr;
    HWND themeButton_ = nullptr;
    HWND refreshButton_ = nullptr;
    HWND columnsButton_ = nullptr;
    HWND openButton_ = nullptr;
    HWND destButtons_[kMaxDestinations] = {};
    HWND deleteButton_ = nullptr;
    HWND skipButton_ = nullptr;
    HWND undoButton_ = nullptr;
    HWND confirmButton_ = nullptr;
    FileList list_;
    PreviewPane preview_;
    HWND lastFocus_ = nullptr;

    // Geometry (physical pixels), recomputed by Layout().
    RECT rcFolder_{}, rcFiles_{}, rcPreview_{}, rcDest_{}, rcStatus_{}, rcArea_{};
    RECT rcPathFrame_{}, rcSummary_{};
    int destHintRight_ = 0;
    Splitter dragging_ = Splitter::None;
    int dragOffset_ = 0;

    // Session state.
    std::wstring summary_;
    std::wstring status_;
    std::wstring statusBase_;
    std::vector<UndoEntry> undo_;
    int handledCount_ = 0;
    bool busy_ = false;
    bool closePending_ = false;
};
