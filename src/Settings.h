// Persistent user settings, stored as UTF-16 INI in %APPDATA%\FileSorter\settings.ini.
#pragma once
#include "Common.h"

struct Destination {
    std::wstring name;
    std::wstring path;
    bool IsSet() const { return !path.empty(); }
};

enum class ThemeMode { System = 0, Light = 1, Dark = 2 };

struct Settings {
    ThemeMode theme = ThemeMode::System;
    std::wstring lastFolder;
    bool confirmDelete = true;

    // Layout (logical 96-DPI pixels; 0 means "use the default").
    double mainSplit = 0.44;
    int leftTopHeight = 0;
    int destHeight = 0;
    RECT windowRect{};  // restored-window bounds in screen pixels
    bool maximized = false;

    // File list.
    int sortColumn = 0;
    bool sortAscending = true;
    unsigned columnMask = 0;          // 0 means defaults
    std::vector<int> columnWidths;    // logical widths per column id (0 = default)

    Destination destinations[kMaxDestinations];

    void Load();
    void Save() const;

private:
    static std::wstring FilePath();
};
