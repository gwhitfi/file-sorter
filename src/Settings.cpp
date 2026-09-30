#include "Settings.h"

namespace {

std::wstring ReadString(const std::wstring& file, const wchar_t* section, const wchar_t* key, const wchar_t* def = L"") {
    std::vector<wchar_t> buf(4096);
    for (;;) {
        DWORD n = GetPrivateProfileStringW(section, key, def, buf.data(), static_cast<DWORD>(buf.size()), file.c_str());
        if (n < buf.size() - 1) return std::wstring(buf.data(), n);
        buf.resize(buf.size() * 2);
    }
}

int ReadInt(const std::wstring& file, const wchar_t* section, const wchar_t* key, int def) {
    return static_cast<int>(GetPrivateProfileIntW(section, key, def, file.c_str()));
}

void WriteString(const std::wstring& file, const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(section, key, value.c_str(), file.c_str());
}

void WriteInt(const std::wstring& file, const wchar_t* section, const wchar_t* key, long long value) {
    WriteString(file, section, key, std::to_wstring(value));
}

}  // namespace

std::wstring Settings::FilePath() {
    PWSTR appData = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)) && appData) {
        dir = JoinPath(appData, L"FileSorter");
        CoTaskMemFree(appData);
    } else {
        dir = L".";
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring file = JoinPath(dir, L"settings.ini");
    if (!PathExists(file)) {
        // A UTF-16 BOM makes the profile APIs store Unicode, so non-ASCII folder names survive.
        HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            const unsigned char bom[2] = {0xFF, 0xFE};
            DWORD written = 0;
            WriteFile(h, bom, 2, &written, nullptr);
            CloseHandle(h);
        }
    }
    return file;
}

void Settings::Load() {
    std::wstring f = FilePath();
    const wchar_t* g = L"General";
    int theme = ReadInt(f, g, L"Theme", 0);
    this->theme = (theme >= 0 && theme <= 2) ? static_cast<ThemeMode>(theme) : ThemeMode::System;
    lastFolder = ReadString(f, g, L"LastFolder");
    confirmDelete = ReadInt(f, g, L"ConfirmDelete", 1) != 0;

    const wchar_t* l = L"Layout";
    int split = ReadInt(f, l, L"MainSplitPermille", 440);
    mainSplit = std::clamp(split, 150, 850) / 1000.0;
    leftTopHeight = std::max(0, ReadInt(f, l, L"LeftTopHeight", 0));
    destHeight = std::max(0, ReadInt(f, l, L"DestinationsHeight", 0));
    windowRect.left = ReadInt(f, l, L"WindowLeft", 0);
    windowRect.top = ReadInt(f, l, L"WindowTop", 0);
    windowRect.right = ReadInt(f, l, L"WindowRight", 0);
    windowRect.bottom = ReadInt(f, l, L"WindowBottom", 0);
    maximized = ReadInt(f, l, L"Maximized", 0) != 0;

    const wchar_t* c = L"Columns";
    sortColumn = ReadInt(f, c, L"SortColumn", 0);
    sortAscending = ReadInt(f, c, L"SortAscending", 1) != 0;
    columnMask = static_cast<unsigned>(ReadInt(f, c, L"VisibleMask", 0));
    columnWidths.clear();
    std::wstring widths = ReadString(f, c, L"Widths");
    size_t start = 0;
    while (start < widths.size()) {
        size_t end = widths.find(L',', start);
        if (end == std::wstring::npos) end = widths.size();
        columnWidths.push_back(_wtoi(widths.substr(start, end - start).c_str()));
        start = end + 1;
    }

    for (int i = 0; i < kMaxDestinations; ++i) {
        std::wstring section = L"Destination" + std::to_wstring(i + 1);
        destinations[i].name = ReadString(f, section.c_str(), L"Name");
        destinations[i].path = ReadString(f, section.c_str(), L"Path");
    }
}

void Settings::Save() const {
    std::wstring f = FilePath();
    const wchar_t* g = L"General";
    WriteInt(f, g, L"Theme", static_cast<int>(theme));
    WriteString(f, g, L"LastFolder", lastFolder);
    WriteInt(f, g, L"ConfirmDelete", confirmDelete ? 1 : 0);

    const wchar_t* l = L"Layout";
    WriteInt(f, l, L"MainSplitPermille", static_cast<int>(mainSplit * 1000 + 0.5));
    WriteInt(f, l, L"LeftTopHeight", leftTopHeight);
    WriteInt(f, l, L"DestinationsHeight", destHeight);
    WriteInt(f, l, L"WindowLeft", windowRect.left);
    WriteInt(f, l, L"WindowTop", windowRect.top);
    WriteInt(f, l, L"WindowRight", windowRect.right);
    WriteInt(f, l, L"WindowBottom", windowRect.bottom);
    WriteInt(f, l, L"Maximized", maximized ? 1 : 0);

    const wchar_t* c = L"Columns";
    WriteInt(f, c, L"SortColumn", sortColumn);
    WriteInt(f, c, L"SortAscending", sortAscending ? 1 : 0);
    WriteInt(f, c, L"VisibleMask", columnMask);
    std::wstring widths;
    for (size_t i = 0; i < columnWidths.size(); ++i) {
        if (i) widths += L',';
        widths += std::to_wstring(columnWidths[i]);
    }
    WriteString(f, c, L"Widths", widths);

    for (int i = 0; i < kMaxDestinations; ++i) {
        std::wstring section = L"Destination" + std::to_wstring(i + 1);
        WriteString(f, section.c_str(), L"Name", destinations[i].name);
        WriteString(f, section.c_str(), L"Path", destinations[i].path);
    }
}
