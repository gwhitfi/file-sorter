#include "PreviewPane.h"

#include <mfapi.h>
#include <mfplay.h>

#include <cstdlib>
#include <richedit.h>

#include "Button.h"
#include "Theme.h"

namespace {

const wchar_t kClassName[] = L"FileSorter.Preview";
const wchar_t kVideoClassName[] = L"FileSorter.Video";
const wchar_t kPreviewHandlerKey[] = L"{8895b1c6-b41f-4c1c-a562-0d564250836f}";

constexpr UINT_PTR kLoadTimer = 1;
constexpr UINT_PTR kMediaTimer = 2;
constexpr int kPlayButtonId = 10;
constexpr DWORD kMaxTextBytes = 1u << 20;  // text preview reads at most 1 MiB
constexpr DWORD kHexBytes = 4096;
constexpr UINT kMaxImageSide = 4096;       // decoded images are capped to bound memory use
// MFP_POSITIONTYPE_100NS is GUID_NULL; the SDK only declares it extern, which some toolchains can't link.
const GUID& kPosition100ns = GUID_NULL;

IWICImagingFactory* Wic() {
    // Intentionally never released: it must outlive every window and is torn down with the process.
    static IWICImagingFactory* factory = nullptr;
    if (!factory)
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    return factory;
}

bool InList(const std::wstring& ext, std::initializer_list<const wchar_t*> list) {
    for (const wchar_t* e : list)
        if (ext == e) return true;
    return false;
}

bool IsKnownTextExtension(const std::wstring& ext) {
    return InList(ext, {L".txt", L".log", L".md", L".markdown", L".csv", L".tsv", L".json", L".jsonc", L".xml",
                        L".yaml", L".yml", L".ini", L".cfg", L".conf", L".toml", L".c", L".h", L".cpp", L".hpp",
                        L".cc", L".cxx", L".hh", L".cs", L".java", L".js", L".mjs", L".cjs", L".ts", L".jsx",
                        L".tsx", L".py", L".rb", L".go", L".rs", L".php", L".sh", L".bash", L".zsh", L".bat",
                        L".cmd", L".ps1", L".psm1", L".psd1", L".sql", L".css", L".scss", L".sass", L".less",
                        L".lua", L".pl", L".r", L".swift", L".kt", L".kts", L".m", L".mm", L".vb", L".vbs",
                        L".gradle", L".properties", L".gitignore", L".gitattributes", L".editorconfig", L".env",
                        L".srt", L".vtt", L".nfo", L".reg", L".tex", L".rst", L".asm", L".s", L".cmake",
                        L".make", L".mk", L".dockerfile", L".diff", L".patch", L".csproj", L".vcxproj",
                        L".sln", L".props", L".targets", L".manifest", L".xaml", L".config", L".inf",
                        L".lst", L".dart", L".scala", L".clj", L".hs", L".ex", L".exs", L".erl", L".f90",
                        L".jl", L".vue", L".svelte", L".graphql", L".proto", L".tf", L".hcl", L".cue",
                        L".adoc", L".org", L".rc", L".def", L".idl"});
}

// Formats that look best in their own renderer (HTML engine, PDF viewer, ...) even though some are text.
bool PrefersHandler(const std::wstring& ext) {
    return InList(ext, {L".htm", L".html", L".xhtml", L".mht", L".mhtml", L".svg", L".rtf", L".pdf", L".xps",
                        L".oxps", L".eml", L".msg", L".ics", L".vcf"});
}

bool IsMediaExtension(const std::wstring& ext) {
    return InList(ext, {L".mp4", L".m4v", L".mov", L".wmv", L".avi", L".mkv", L".webm", L".3gp", L".3g2",
                        L".mpg", L".mpeg", L".m2ts", L".mts", L".ts", L".asf", L".mp3", L".wav", L".wma",
                        L".m4a", L".aac", L".flac", L".ogg", L".opus", L".aiff", L".aif", L".amr", L".ac3",
                        L".ec3"});
}

bool IsShellArchive(const std::wstring& ext) {
    return InList(ext, {L".7z", L".rar", L".tar", L".tgz", L".gz", L".bz2", L".tbz2", L".xz", L".txz", L".cab",
                        L".zst", L".lz", L".lzma"});
}

bool RegDefault(const std::wstring& subKey, std::wstring& out) {
    wchar_t buf[256] = {};
    DWORD cb = sizeof(buf);
    if (RegGetValueW(HKEY_CLASSES_ROOT, subKey.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, buf, &cb) == ERROR_SUCCESS) {
        out = buf;
        return !out.empty();
    }
    return false;
}

// Finds the preview handler Explorer would use for this extension.
bool FindPreviewHandler(const std::wstring& ext, CLSID* clsid) {
    if (ext.empty()) return false;
    std::wstring value;
    wchar_t buf[128] = {};
    DWORD cch = 128;
    if (SUCCEEDED(AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_SHELLEXTENSION, ext.c_str(), kPreviewHandlerKey, buf, &cch)))
        value = buf;
    const std::wstring suffix = std::wstring(L"\\ShellEx\\") + kPreviewHandlerKey;
    if (value.empty()) RegDefault(ext + suffix, value);
    if (value.empty()) {
        std::wstring progId;
        if (RegDefault(ext, progId)) RegDefault(progId + suffix, value);
    }
    if (value.empty()) RegDefault(L"SystemFileAssociations\\" + ext + suffix, value);
    if (value.empty()) {
        wchar_t perceived[64] = {};
        DWORD cb = sizeof(perceived);
        if (RegGetValueW(HKEY_CLASSES_ROOT, ext.c_str(), L"PerceivedType", RRF_RT_REG_SZ, nullptr, perceived, &cb) ==
            ERROR_SUCCESS)
            RegDefault(L"SystemFileAssociations\\" + std::wstring(perceived) + suffix, value);
    }
    return !value.empty() && SUCCEEDED(CLSIDFromString(value.c_str(), clsid));
}

bool ReadHead(const std::wstring& path, DWORD maxBytes, std::vector<unsigned char>& out) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    out.resize(maxBytes);
    DWORD total = 0;
    while (total < maxBytes) {
        DWORD read = 0;
        if (!ReadFile(h, out.data() + total, maxBytes - total, &read, nullptr) || read == 0) break;
        total += read;
    }
    CloseHandle(h);
    out.resize(total);
    return true;
}

bool LooksLikeUtf16Le(const unsigned char* b, size_t n) {
    if (n < 8) return false;
    size_t pairs = std::min<size_t>(n / 2, 256), zeroHigh = 0;
    for (size_t i = 0; i < pairs; ++i)
        if (b[i * 2 + 1] == 0 && b[i * 2] != 0) ++zeroHigh;
    return zeroHigh * 10 >= pairs * 9;
}

bool LooksLikeText(const std::vector<unsigned char>& bytes) {
    size_t n = std::min<size_t>(bytes.size(), 8192);
    if (n == 0) return false;
    const unsigned char* b = bytes.data();
    if ((n >= 2 && ((b[0] == 0xFF && b[1] == 0xFE) || (b[0] == 0xFE && b[1] == 0xFF))) ||
        (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF))
        return true;
    if (LooksLikeUtf16Le(b, n)) return true;
    size_t control = 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = b[i];
        if (c == 0) return false;
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != 0x1B) ++control;
    }
    return control * 100 <= n;
}

std::wstring DecodeText(const std::vector<unsigned char>& bytes, bool truncated) {
    const unsigned char* b = bytes.data();
    size_t n = bytes.size();
    std::wstring text;
    if (n >= 2 && b[0] == 0xFF && b[1] == 0xFE) {
        text.assign(reinterpret_cast<const wchar_t*>(b + 2), (n - 2) / 2);
    } else if (n >= 2 && b[0] == 0xFE && b[1] == 0xFF) {
        for (size_t i = 2; i + 1 < n; i += 2) text.push_back(static_cast<wchar_t>((b[i] << 8) | b[i + 1]));
    } else if (LooksLikeUtf16Le(b, n)) {
        text.assign(reinterpret_cast<const wchar_t*>(b), n / 2);
    } else {
        size_t start = (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) ? 3 : 0;
        const char* src = reinterpret_cast<const char*>(b + start);
        int len = static_cast<int>(n - start);
        int wlen = 0;
        // A truncated read may split a multi-byte sequence; retry with up to 3 bytes trimmed.
        for (int trim = 0; trim <= (truncated ? 3 : 0) && len - trim > 0; ++trim) {
            wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, len - trim, nullptr, 0);
            if (wlen > 0) {
                len -= trim;
                break;
            }
        }
        UINT codePage = wlen > 0 ? CP_UTF8 : CP_ACP;
        DWORD flags = wlen > 0 ? MB_ERR_INVALID_CHARS : 0;
        wlen = MultiByteToWideChar(codePage, flags, src, len, nullptr, 0);
        text.resize(static_cast<size_t>(std::max(wlen, 0)));
        if (wlen > 0) MultiByteToWideChar(codePage, flags, src, len, text.data(), wlen);
    }
    // The edit control wants CRLF line endings and stops at embedded NULs.
    std::wstring out;
    out.reserve(text.size() + text.size() / 16);
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t c = text[i];
        if (c == L'\r') {
            out += L"\r\n";
            if (i + 1 < text.size() && text[i + 1] == L'\n') ++i;
        } else if (c == L'\n') {
            out += L"\r\n";
        } else if (c == 0) {
            out += L' ';
        } else {
            out += c;
        }
    }
    return out;
}

std::wstring HexDump(const std::vector<unsigned char>& bytes) {
    std::wstring out;
    wchar_t line[128];
    for (size_t off = 0; off < bytes.size(); off += 16) {
        int pos = swprintf(line, 128, L"%08zX  ", off);
        for (size_t i = 0; i < 16; ++i) {
            if (off + i < bytes.size())
                pos += swprintf(line + pos, 128 - pos, L"%02X ", bytes[off + i]);
            else
                pos += swprintf(line + pos, 128 - pos, L"   ");
            if (i == 7) line[pos++] = L' ';
        }
        line[pos++] = L' ';
        for (size_t i = 0; i < 16 && off + i < bytes.size(); ++i) {
            unsigned char c = bytes[off + i];
            line[pos++] = (c >= 0x20 && c < 0x7F) ? static_cast<wchar_t>(c) : L'.';
        }
        line[pos] = 0;
        out += line;
        out += L"\r\n";
    }
    return out;
}

std::wstring FormatDuration(long long hns) {
    long long total = hns / 10000000;
    wchar_t buf[32];
    if (total >= 3600)
        swprintf(buf, 32, L"%lld:%02lld:%02lld", total / 3600, (total / 60) % 60, total % 60);
    else
        swprintf(buf, 32, L"%lld:%02lld", total / 60, total % 60);
    return buf;
}

// --- ZIP central directory reader (covers .zip, .jar, .apk, .epub, .nupkg, .vsix, ...) ---

bool ReadAt(HANDLE h, unsigned long long offset, void* buf, DWORD len) {
    LARGE_INTEGER li;
    li.QuadPart = static_cast<LONGLONG>(offset);
    DWORD read = 0;
    return SetFilePointerEx(h, li, nullptr, FILE_BEGIN) && ReadFile(h, buf, len, &read, nullptr) && read == len;
}

unsigned U16(const unsigned char* p) { return p[0] | (p[1] << 8); }
unsigned long U32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<unsigned long>(p[3]) << 24); }
unsigned long long U64(const unsigned char* p) { return U32(p) | (static_cast<unsigned long long>(U32(p + 4)) << 32); }

bool ListZip(const std::wstring& path, std::wstring& out, size_t& count) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    struct Closer { HANDLE h; ~Closer() { CloseHandle(h); } } closer{h};

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 22) return false;
    unsigned long long fileSize = static_cast<unsigned long long>(size.QuadPart);
    DWORD tailLen = static_cast<DWORD>(std::min<unsigned long long>(fileSize, 65557));
    std::vector<unsigned char> tail(tailLen);
    if (!ReadAt(h, fileSize - tailLen, tail.data(), tailLen)) return false;

    long eocd = -1;
    for (long i = static_cast<long>(tailLen) - 22; i >= 0; --i)
        if (U32(&tail[i]) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) return false;

    unsigned long long entries = U16(&tail[eocd + 10]);
    unsigned long long cdSize = U32(&tail[eocd + 12]);
    unsigned long long cdOffset = U32(&tail[eocd + 16]);
    if (cdOffset == 0xFFFFFFFF || entries == 0xFFFF) {  // ZIP64
        if (eocd < 20 || U32(&tail[eocd - 20]) != 0x07064b50) return false;
        unsigned long long recOffset = U64(&tail[eocd - 20 + 8]);
        unsigned char rec[56];
        if (!ReadAt(h, recOffset, rec, sizeof(rec)) || U32(rec) != 0x06064b50) return false;
        entries = U64(rec + 32);
        cdSize = U64(rec + 40);
        cdOffset = U64(rec + 48);
    }
    if (cdSize == 0 || cdSize > (64ull << 20) || cdOffset + cdSize > fileSize) return false;
    std::vector<unsigned char> cd(static_cast<size_t>(cdSize));
    if (!ReadAt(h, cdOffset, cd.data(), static_cast<DWORD>(cdSize))) return false;

    const size_t kMaxListed = 2000;
    unsigned long long totalSize = 0;
    std::wstring lines;
    size_t p = 0;
    count = 0;
    while (p + 46 <= cd.size() && U32(&cd[p]) == 0x02014b50) {
        unsigned flags = U16(&cd[p + 8]);
        unsigned long long usize = U32(&cd[p + 24]);
        unsigned nameLen = U16(&cd[p + 28]), extraLen = U16(&cd[p + 30]), commentLen = U16(&cd[p + 32]);
        if (p + 46 + nameLen + extraLen > cd.size()) break;
        const char* name = reinterpret_cast<const char*>(&cd[p + 46]);
        if (usize == 0xFFFFFFFF) {  // real size lives in the ZIP64 extra field
            size_t e = p + 46 + nameLen, end = e + extraLen;
            while (e + 4 <= end) {
                unsigned id = U16(&cd[e]), len = U16(&cd[e + 2]);
                if (id == 1 && len >= 8 && e + 12 <= end) { usize = U64(&cd[e + 4]); break; }
                e += 4 + len;
            }
        }
        UINT codePage = (flags & 0x800) ? CP_UTF8 : 437;
        int wlen = MultiByteToWideChar(codePage, 0, name, static_cast<int>(nameLen), nullptr, 0);
        std::wstring wname(static_cast<size_t>(std::max(wlen, 0)), L'\0');
        if (wlen > 0) MultiByteToWideChar(codePage, 0, name, static_cast<int>(nameLen), wname.data(), wlen);
        bool isDir = !wname.empty() && (wname.back() == L'/' || wname.back() == L'\\');
        if (count < kMaxListed) {
            wchar_t sizeCol[32];
            swprintf(sizeCol, 32, L"%12s   ", isDir ? L"" : FormatBytes(usize).c_str());
            lines += sizeCol;
            lines += wname;
            lines += L"\r\n";
        }
        totalSize += usize;
        ++count;
        p += 46 + nameLen + extraLen + commentLen;
    }
    if (count == 0) return false;
    out = FormatNumber(count) + (count == 1 ? L" item" : L" items") + L", " + FormatBytes(totalSize) +
          L" uncompressed\r\n\r\n" + lines;
    if (count > kMaxListed) out += L"\r\n… and " + FormatNumber(count - kMaxListed) + L" more";
    return true;
}

}  // namespace

// Receives Media Foundation player events and forwards them to the UI thread.
class MediaCallback : public IMFPMediaPlayerCallback {
public:
    MediaCallback(HWND hwnd, UINT generation) : hwnd_(hwnd), generation_(generation) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(IMFPMediaPlayerCallback)) {
            *ppv = static_cast<IMFPMediaPlayerCallback*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }
    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* e) override {
        HWND target = hwnd_;
        if (target && e)
            PostMessageW(target, WM_APP_MEDIAEVENT, MAKEWPARAM(e->eEventType, generation_ & 0xFFFF),
                         static_cast<LPARAM>(e->hrEvent));
    }
    void Detach() { hwnd_ = nullptr; }

private:
    virtual ~MediaCallback() = default;
    volatile LONG ref_ = 1;
    HWND volatile hwnd_;
    UINT generation_;
};

bool PreviewPane::Create(HWND parent, int id) {
    static bool registered = false;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        wc.lpfnWndProc = VideoProc;
        wc.lpszClassName = kVideoClassName;
        RegisterClassExW(&wc);
        registered = true;
    }
    hwnd_ = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0, parent,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, this);
    if (!hwnd_) return false;

    // RichEdit rather than EDIT: faster with large files and scrollbars appear only when needed.
    LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    edit_ = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
                            WS_CHILD | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
                                ES_AUTOHSCROLL | ES_NOHIDESEL,
                            0, 0, 0, 0, hwnd_, nullptr, inst, nullptr);
    SendMessageW(edit_, EM_SETTEXTMODE, TM_PLAINTEXT | TM_MULTILEVELUNDO | TM_MULTICODEPAGE, 0);
    SendMessageW(edit_, EM_EXLIMITTEXT, 0, 64 << 20);
    SendMessageW(edit_, EM_SETTARGETDEVICE, 0, 1);  // no word wrap
    SendMessageW(edit_, EM_SETUNDOLIMIT, 0, 0);
    video_ = CreateWindowExW(0, kVideoClassName, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, hwnd_, nullptr, inst, this);
    playButton_ = Button::Create(hwnd_, kPlayButtonId, ButtonKind::Primary, L"", Glyph::Play, L"Play / pause");
    ShowWindow(playButton_, SW_HIDE);
    OnFontsChanged();
    OnThemeChanged();
    ShowPlaceholder(Glyph::FolderOpen, L"Nothing to preview yet", L"Choose a folder to sort and its files will appear here.");
    return true;
}

// ---------------------------------------------------------------------------------------------
// Loading

void PreviewPane::ShowFile(const std::wstring& path, const FileEntry& entry) {
    if (!path_.empty() && CompareStringOrdinal(path.c_str(), -1, path_.c_str(), -1, TRUE) == CSTR_EQUAL &&
        mode_ != Mode::Placeholder)
        return;
    UnloadContent();
    path_ = path;
    entry_ = entry;
    mode_ = Mode::Loading;
    statusText_.clear();  // stays blank during the short debounce to avoid flicker while arrowing through files

    SHFILEINFOW sfi{};
    if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON)) fileIcon_ = sfi.hIcon;

    InvalidateRect(hwnd_, nullptr, FALSE);
    SetTimer(hwnd_, kLoadTimer, 90, nullptr);
}

void PreviewPane::ShowPlaceholder(wchar_t glyph, const std::wstring& title, const std::wstring& subtitle) {
    UnloadContent();
    KillTimer(hwnd_, kLoadTimer);
    path_.clear();
    mode_ = Mode::Placeholder;
    placeholderGlyph_ = glyph;
    placeholderTitle_ = title;
    placeholderSubtitle_ = subtitle;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PreviewPane::ReleaseFile(const std::wstring& message) {
    KillTimer(hwnd_, kLoadTimer);
    UnloadContent();
    path_.clear();
    mode_ = Mode::Loading;
    statusText_ = message;
    InvalidateRect(hwnd_, nullptr, FALSE);
    UpdateWindow(hwnd_);
}

void PreviewPane::UnloadContent() {
    StopMedia();
    if (handler_) {
        handler_->Unload();
        handler_.Reset();
    }
    handlerStream_.Reset();
    image_.Reset();
    if (scaled_) DeleteObject(scaled_);
    scaled_ = nullptr;
    scaledSize_ = {};
    imageHasAlpha_ = false;
    if (fileIcon_) DestroyIcon(fileIcon_);
    fileIcon_ = nullptr;
    ShowWindow(edit_, SW_HIDE);
    SetWindowTextW(edit_, L"");
    ShowWindow(playButton_, SW_HIDE);
    kind_.clear();
    extraInfo_.clear();
    mediaError_.clear();
}

void PreviewPane::LoadCurrent() {
    if (path_.empty()) return;
    if (!PathExists(path_)) {
        std::wstring name = entry_.name;
        ShowPlaceholder(Glyph::Warning, L"File not found", L"“" + name + L"” was moved or deleted outside File Sorter. Press F5 to refresh.");
        return;
    }
    const std::wstring& ext = entry_.ext;
    PERCEIVED perceived = PERCEIVED_TYPE_UNSPECIFIED;
    PERCEIVEDFLAG perceivedFlags = 0;
    if (!ext.empty()) AssocGetPerceivedType(ext.c_str(), &perceived, &perceivedFlags, nullptr);

    if (entry_.size == 0) {
        kind_ = L"Empty";
        mode_ = Mode::Placeholder;
        placeholderGlyph_ = Glyph::Document;
        placeholderTitle_ = L"Empty file";
        placeholderSubtitle_ = L"This file contains no data (0 bytes).";
        // Keep path_ so the info strip still describes the file.
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (ext != L".svg" && TryImage()) return;

    if (perceived == PERCEIVED_TYPE_VIDEO || perceived == PERCEIVED_TYPE_AUDIO || IsMediaExtension(ext)) {
        ShowMediaView();
        return;
    }

    CLSID handler{};
    bool hasHandler = FindPreviewHandler(ext, &handler);
    bool textType = perceived == PERCEIVED_TYPE_TEXT || IsKnownTextExtension(ext);

    if (textType && !(hasHandler && PrefersHandler(ext)) && TryText(false)) return;
    if (hasHandler) {
        statusText_ = L"Loading preview…";
        InvalidateRect(hwnd_, nullptr, FALSE);
        UpdateWindow(hwnd_);
        if (TryHandler(handler)) return;
    }
    if (TryZipListing()) return;
    if (IsShellArchive(ext) && TryShellArchive()) return;
    if (TryText(true)) return;
    if (TryThumbnail()) return;
    ShowHex();
}

bool PreviewPane::TryImage() {
    IWICImagingFactory* wic = Wic();
    if (!wic) return false;
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateDecoderFromFilename(path_.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                              decoder.Put())))
        return false;
    UINT frameCount = 0;
    if (FAILED(decoder->GetFrameCount(&frameCount)) || frameCount == 0) return false;

    // Icons hold several sizes: show the largest.
    UINT best = 0;
    GUID container{};
    decoder->GetContainerFormat(&container);
    if (container == GUID_ContainerFormatIco && frameCount > 1) {
        UINT bestArea = 0;
        for (UINT i = 0; i < frameCount; ++i) {
            ComPtr<IWICBitmapFrameDecode> f;
            UINT w = 0, h = 0;
            if (SUCCEEDED(decoder->GetFrame(i, f.Put())) && SUCCEEDED(f->GetSize(&w, &h)) && w * h > bestArea) {
                bestArea = w * h;
                best = i;
            }
        }
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(best, frame.Put()))) return false;
    UINT width = 0, height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0) return false;

    WICPixelFormatGUID format{};
    frame->GetPixelFormat(&format);
    {
        ComPtr<IWICComponentInfo> info;
        ComPtr<IWICPixelFormatInfo2> pfInfo;
        BOOL transparent = FALSE;
        if (SUCCEEDED(wic->CreateComponentInfo(format, info.Put())) && SUCCEEDED(info.As(pfInfo)))
            pfInfo->SupportsTransparency(&transparent);
        imageHasAlpha_ = transparent != FALSE;
    }

    // EXIF orientation (photos from phones are usually stored sideways).
    UINT orientation = 1;
    {
        ComPtr<IWICMetadataQueryReader> query;
        if (SUCCEEDED(frame->GetMetadataQueryReader(query.Put()))) {
            for (const wchar_t* q : {L"System.Photo.Orientation", L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}"}) {
                PROPVARIANT v;
                PropVariantInit(&v);
                if (SUCCEEDED(query->GetMetadataByName(q, &v))) {
                    if (v.vt == VT_UI2 && v.uiVal >= 1 && v.uiVal <= 8) orientation = v.uiVal;
                    PropVariantClear(&v);
                    if (orientation != 1) break;
                }
            }
        }
    }

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wic->CreateFormatConverter(converter.Put())) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                     WICBitmapPaletteTypeMedianCut)))
        return false;
    ComPtr<IWICBitmapSource> source;
    converter.As(source);

    if (width > kMaxImageSide || height > kMaxImageSide) {
        double s = std::min(static_cast<double>(kMaxImageSide) / width, static_cast<double>(kMaxImageSide) / height);
        ComPtr<IWICBitmapScaler> scaler;
        if (SUCCEEDED(wic->CreateBitmapScaler(scaler.Put())) &&
            SUCCEEDED(scaler->Initialize(source.Get(), std::max(1u, static_cast<UINT>(width * s)),
                                         std::max(1u, static_cast<UINT>(height * s)), WICBitmapInterpolationModeFant)))
            scaler.As(source);
    }

    ComPtr<IWICBitmap> bitmap;
    if (FAILED(wic->CreateBitmapFromSource(source.Get(), WICBitmapCacheOnLoad, bitmap.Put()))) return false;
    bitmap.As(source);

    if (orientation != 1) {
        WICBitmapTransformOptions opts = WICBitmapTransformRotate0;
        switch (orientation) {
        case 2: opts = WICBitmapTransformFlipHorizontal; break;
        case 3: opts = WICBitmapTransformRotate180; break;
        case 4: opts = WICBitmapTransformFlipVertical; break;
        case 5: opts = static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal); break;
        case 6: opts = WICBitmapTransformRotate90; break;
        case 7: opts = static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal); break;
        case 8: opts = WICBitmapTransformRotate270; break;
        }
        ComPtr<IWICBitmapFlipRotator> rotator;
        ComPtr<IWICBitmap> rotated;
        if (SUCCEEDED(wic->CreateBitmapFlipRotator(rotator.Put())) && SUCCEEDED(rotator->Initialize(source.Get(), opts)) &&
            SUCCEEDED(wic->CreateBitmapFromSource(rotator.Get(), WICBitmapCacheOnLoad, rotated.Put())))
            rotated.As(source);
        if (orientation >= 5) std::swap(width, height);
    }

    image_ = source;
    mode_ = Mode::Image;
    kind_ = frameCount > 1 && container != GUID_ContainerFormatIco ? L"Image · frame 1 of " + std::to_wstring(frameCount) : L"Image";
    extraInfo_ = std::to_wstring(width) + L" × " + std::to_wstring(height) + L" px";
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool PreviewPane::TryText(bool requireTextLike) {
    std::vector<unsigned char> bytes;
    if (!ReadHead(path_, kMaxTextBytes, bytes)) return false;
    if (!LooksLikeText(bytes)) {
        if (requireTextLike) return false;
        if (bytes.empty()) return false;
        // Known text extension but binary content: let other renderers try.
        size_t nuls = std::count(bytes.begin(), bytes.begin() + std::min<size_t>(bytes.size(), 8192), 0);
        if (nuls > 0 && !LooksLikeUtf16Le(bytes.data(), bytes.size())) return false;
    }
    bool truncated = entry_.size > bytes.size();
    std::wstring text = DecodeText(bytes, truncated);
    size_t lines = static_cast<size_t>(std::count(text.begin(), text.end(), L'\n'));
    if (!text.empty() && text.back() != L'\n') ++lines;
    extraInfo_ = FormatNumber(lines) + (lines == 1 ? L" line" : L" lines");
    if (truncated) extraInfo_ += L" (first " + FormatBytes(bytes.size()) + L" shown)";
    ShowTextContent(text, L"Text");
    return true;
}

void PreviewPane::ShowTextContent(const std::wstring& text, const std::wstring& kind) {
    mode_ = Mode::Text;
    kind_ = kind;
    SetWindowTextW(edit_, text.c_str());
    ApplyEditColors();
    SendMessageW(edit_, EM_SETSEL, 0, 0);
    SendMessageW(edit_, EM_SCROLLCARET, 0, 0);
    Layout();
    ShowWindow(edit_, SW_SHOW);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool PreviewPane::TryHandler(const CLSID& clsid) {
    ComPtr<IPreviewHandler> handler;
    // Out-of-process first (prevhost.exe, like Explorer) so a misbehaving handler can't take us down.
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(handler.Put()));
    if (FAILED(hr)) hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(handler.Put()));
    if (FAILED(hr)) return false;

    bool initialized = false;
    ComPtr<IInitializeWithStream> withStream;
    if (SUCCEEDED(handler.As(withStream))) {
        ComPtr<IStream> stream;
        if (SUCCEEDED(SHCreateStreamOnFileEx(path_.c_str(), STGM_READ | STGM_SHARE_DENY_NONE, FILE_ATTRIBUTE_NORMAL,
                                             FALSE, nullptr, stream.Put())) &&
            SUCCEEDED(withStream->Initialize(stream.Get(), STGM_READ))) {
            handlerStream_ = stream;
            initialized = true;
        }
    }
    if (!initialized) {
        ComPtr<IInitializeWithItem> withItem;
        ComPtr<IShellItem> item;
        if (SUCCEEDED(handler.As(withItem)) &&
            SUCCEEDED(SHCreateItemFromParsingName(path_.c_str(), nullptr, IID_PPV_ARGS(item.Put()))) &&
            SUCCEEDED(withItem->Initialize(item.Get(), STGM_READ)))
            initialized = true;
    }
    if (!initialized) {
        ComPtr<IInitializeWithFile> withFile;
        if (SUCCEEDED(handler.As(withFile)) && SUCCEEDED(withFile->Initialize(path_.c_str(), STGM_READ)))
            initialized = true;
    }
    if (!initialized) {
        handlerStream_.Reset();
        return false;
    }

    ComPtr<IPreviewHandlerVisuals> visuals;
    if (SUCCEEDED(handler.As(visuals))) {
        const Palette& p = Theme::Colors();
        visuals->SetBackgroundColor(p.surface);
        visuals->SetTextColor(p.text);
        LOGFONTW lf{};
        if (GetObjectW(g_fonts.ui, sizeof(lf), &lf)) visuals->SetFont(&lf);
    }

    RECT rc = ContentRect();
    kind_ = L"Document";
    mode_ = Mode::Handler;
    if (FAILED(handler->SetWindow(hwnd_, &rc)) || FAILED(handler->SetRect(&rc)) || FAILED(handler->DoPreview())) {
        handler->Unload();
        handlerStream_.Reset();
        mode_ = Mode::Loading;
        kind_.clear();
        return false;
    }
    handler_ = handler;
    statusText_.clear();
    InvalidateRect(hwnd_, nullptr, FALSE);
    // Some handlers grab keyboard focus; hand it back so the list keeps its shortcuts.
    PostMessageW(GetParent(hwnd_), WM_APP_RESTOREFOCUS, 0, 0);
    return true;
}

bool PreviewPane::TryZipListing() {
    std::vector<unsigned char> magic;
    if (!ReadHead(path_, 4, magic) || magic.size() < 4 || magic[0] != 'P' || magic[1] != 'K') return false;
    std::wstring listing;
    size_t count = 0;
    if (!ListZip(path_, listing, count)) return false;
    extraInfo_ = FormatNumber(count) + (count == 1 ? L" item" : L" items");
    ShowTextContent(listing, L"Archive contents");
    return true;
}

bool PreviewPane::TryShellArchive() {
    // Windows 11 can browse 7z, RAR, TAR and more through the shell; older systems simply fail here.
    ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(path_.c_str(), nullptr, IID_PPV_ARGS(item.Put())))) return false;
    ComPtr<IEnumShellItems> items;
    if (FAILED(item->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(items.Put())))) return false;
    std::wstring lines;
    size_t count = 0;
    IShellItem* child = nullptr;
    while (count < 2000 && items->Next(1, &child, nullptr) == S_OK) {
        PWSTR name = nullptr;
        if (SUCCEEDED(child->GetDisplayName(SIGDN_PARENTRELATIVEEDITING, &name)) && name) {
            SFGAOF attrs = 0;
            child->GetAttributes(SFGAO_FOLDER, &attrs);
            lines += name;
            if (attrs & SFGAO_FOLDER) lines += L"\\";
            lines += L"\r\n";
            CoTaskMemFree(name);
            ++count;
        }
        child->Release();
    }
    if (count == 0) return false;
    extraInfo_ = FormatNumber(count) + (count == 1 ? L" top-level item" : L" top-level items");
    ShowTextContent(lines, L"Archive contents");
    return true;
}

bool PreviewPane::SetImageFromHBitmap(HBITMAP bitmap) {
    IWICImagingFactory* wic = Wic();
    if (!wic || !bitmap) return false;
    // Shell bitmaps often carry an all-zero alpha channel for opaque images; treat those as opaque.
    bool anyAlpha = false, anyTransparent = false;
    DIBSECTION ds{};
    if (GetObjectW(bitmap, sizeof(ds), &ds) == sizeof(ds) && ds.dsBm.bmBitsPixel == 32 && ds.dsBm.bmBits) {
        const unsigned char* px = static_cast<const unsigned char*>(ds.dsBm.bmBits);
        size_t count = static_cast<size_t>(ds.dsBm.bmWidth) * static_cast<size_t>(std::abs(ds.dsBm.bmHeight));
        for (size_t i = 0; i < count; ++i) {
            unsigned char a = px[i * 4 + 3];
            if (a) anyAlpha = true;
            if (a != 255) anyTransparent = true;
        }
    }
    ComPtr<IWICBitmap> raw;
    if (FAILED(wic->CreateBitmapFromHBITMAP(bitmap, nullptr, anyAlpha ? WICBitmapUsePremultipliedAlpha : WICBitmapIgnoreAlpha,
                                            raw.Put())))
        return false;
    ComPtr<IWICFormatConverter> converter;
    ComPtr<IWICBitmap> converted;
    if (FAILED(wic->CreateFormatConverter(converter.Put())) ||
        FAILED(converter->Initialize(raw.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                     WICBitmapPaletteTypeCustom)) ||
        FAILED(wic->CreateBitmapFromSource(converter.Get(), WICBitmapCacheOnLoad, converted.Put())))
        return false;
    converted.As(image_);
    imageHasAlpha_ = anyAlpha && anyTransparent;
    return true;
}

bool PreviewPane::TryThumbnail() {
    ComPtr<IShellItemImageFactory> factory;
    if (FAILED(SHCreateItemFromParsingName(path_.c_str(), nullptr, IID_PPV_ARGS(factory.Put())))) return false;
    HBITMAP bitmap = nullptr;
    SIZE size = {S(768), S(768)};
    if (FAILED(factory->GetImage(size, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK, &bitmap)) || !bitmap) return false;
    bool ok = SetImageFromHBitmap(bitmap);
    DeleteObject(bitmap);
    if (!ok) return false;
    mode_ = Mode::Image;
    kind_ = L"Thumbnail";
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void PreviewPane::ShowHex() {
    std::vector<unsigned char> bytes;
    if (!ReadHead(path_, kHexBytes, bytes)) {
        DWORD err = GetLastError();
        std::wstring name = entry_.name;
        ShowPlaceholder(Glyph::Warning, L"Can't open this file", ErrorText(err));
        return;
    }
    extraInfo_ = entry_.size > bytes.size() ? L"first " + FormatBytes(bytes.size()) + L" shown" : L"";
    ShowTextContent(HexDump(bytes), L"Binary · hex view");
}

// ---------------------------------------------------------------------------------------------
// Media

void PreviewPane::ShowMediaView() {
    // Poster frame or album art from the shell, if any; otherwise the file icon is drawn.
    ComPtr<IShellItemImageFactory> factory;
    if (SUCCEEDED(SHCreateItemFromParsingName(path_.c_str(), nullptr, IID_PPV_ARGS(factory.Put())))) {
        HBITMAP bitmap = nullptr;
        SIZE size = {S(768), S(768)};
        if (SUCCEEDED(factory->GetImage(size, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK, &bitmap)) && bitmap) {
            SetImageFromHBitmap(bitmap);
            DeleteObject(bitmap);
        }
    }
    PERCEIVED perceived = PERCEIVED_TYPE_UNSPECIFIED;
    PERCEIVEDFLAG flags = 0;
    AssocGetPerceivedType(entry_.ext.c_str(), &perceived, &flags, nullptr);
    kind_ = perceived == PERCEIVED_TYPE_AUDIO ? L"Audio" : L"Video";
    mode_ = Mode::Media;
    durationHns_ = positionHns_ = 0;
    Button::SetGlyph(playButton_, Glyph::Play);
    Layout();
    ShowWindow(playButton_, SW_SHOW);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PreviewPane::StartOrTogglePlayback() {
    if (mode_ != Mode::Media) return;
    if (player_) {
        if (playing_) {
            player_->Pause();
        } else {
            if (durationHns_ > 0 && positionHns_ >= durationHns_ - 1000000) {
                PROPVARIANT pv;
                PropVariantInit(&pv);
                pv.vt = VT_I8;
                pv.hVal.QuadPart = 0;
                player_->SetPosition(kPosition100ns, &pv);
            }
            player_->Play();
        }
        return;
    }
    mediaError_.clear();
    ++mediaGeneration_;
    callback_ = new MediaCallback(hwnd_, mediaGeneration_);
    Layout();
    HRESULT hr = MFPCreateMediaPlayer(path_.c_str(), TRUE, 0, callback_, video_, &player_);
    if (FAILED(hr)) {
        StopMedia();
        mediaError_ = L"This file can't be played. A codec may be missing.";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    playing_ = true;
    Button::SetGlyph(playButton_, Glyph::Pause);
    SetTimer(hwnd_, kMediaTimer, 250, nullptr);
}

void PreviewPane::StopMedia() {
    KillTimer(hwnd_, kMediaTimer);
    if (callback_) callback_->Detach();
    if (player_) {
        player_->Shutdown();
        player_->Release();
        player_ = nullptr;
    }
    if (callback_) {
        callback_->Release();
        callback_ = nullptr;
    }
    playing_ = false;
    hasVideo_ = false;
    if (video_) ShowWindow(video_, SW_HIDE);
    if (playButton_) Button::SetGlyph(playButton_, Glyph::Play);
}

void PreviewPane::OnMediaEvent(UINT type, HRESULT hr) {
    if (!player_) return;
    if (FAILED(hr)) {
        StopMedia();
        mediaError_ = L"This file can't be played. A codec may be missing.";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    switch (type) {
    case MFP_EVENT_TYPE_MEDIAITEM_SET: {
        BOOL hasVideo = FALSE, selected = FALSE;
        IMFPMediaItem* item = nullptr;
        if (SUCCEEDED(player_->GetMediaItem(&item)) && item) {
            item->HasVideo(&hasVideo, &selected);
            item->Release();
        }
        hasVideo_ = hasVideo && selected;
        Layout();
        ShowWindow(video_, hasVideo_ ? SW_SHOW : SW_HIDE);
        UpdateMediaPosition();
        break;
    }
    case MFP_EVENT_TYPE_PLAY:
        playing_ = true;
        break;
    case MFP_EVENT_TYPE_PAUSE:
    case MFP_EVENT_TYPE_STOP:
        playing_ = false;
        break;
    case MFP_EVENT_TYPE_PLAYBACK_ENDED:
        playing_ = false;
        positionHns_ = durationHns_;
        break;
    default:
        break;
    }
    Button::SetGlyph(playButton_, playing_ ? Glyph::Pause : Glyph::Play);
    InvalidateRect(hwnd_, &trackRect_, FALSE);
    RECT bar = MediaBarRect();
    InvalidateRect(hwnd_, &bar, FALSE);
}

void PreviewPane::UpdateMediaPosition() {
    if (!player_) return;
    PROPVARIANT pv;
    PropVariantInit(&pv);
    if (SUCCEEDED(player_->GetDuration(kPosition100ns, &pv)) && pv.vt == VT_UI8) durationHns_ = static_cast<long long>(pv.uhVal.QuadPart);
    else if (pv.vt == VT_I8) durationHns_ = pv.hVal.QuadPart;
    PropVariantClear(&pv);
    if (playing_ || positionHns_ != durationHns_) {
        if (SUCCEEDED(player_->GetPosition(kPosition100ns, &pv))) {
            if (pv.vt == VT_I8) positionHns_ = pv.hVal.QuadPart;
            else if (pv.vt == VT_UI8) positionHns_ = static_cast<long long>(pv.uhVal.QuadPart);
        }
        PropVariantClear(&pv);
    }
    RECT bar = MediaBarRect();
    InvalidateRect(hwnd_, &bar, FALSE);
}

void PreviewPane::SeekTo(int x) {
    if (!player_ || durationHns_ <= 0 || trackRect_.right <= trackRect_.left) return;
    double frac = std::clamp(static_cast<double>(x - trackRect_.left) / (trackRect_.right - trackRect_.left), 0.0, 1.0);
    PROPVARIANT pv;
    PropVariantInit(&pv);
    pv.vt = VT_I8;
    pv.hVal.QuadPart = static_cast<LONGLONG>(frac * durationHns_);
    player_->SetPosition(kPosition100ns, &pv);
    positionHns_ = pv.hVal.QuadPart;
    RECT bar = MediaBarRect();
    InvalidateRect(hwnd_, &bar, FALSE);
}

// ---------------------------------------------------------------------------------------------
// Layout & painting

RECT PreviewPane::ContentRect() const {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    rc.top = std::min(rc.bottom, static_cast<LONG>(S(68) + 1));
    return rc;
}

RECT PreviewPane::MediaBarRect() const {
    RECT rc = ContentRect();
    rc.top = std::max(rc.top, rc.bottom - static_cast<LONG>(S(56)));
    return rc;
}

void PreviewPane::Layout() {
    RECT content = ContentRect();
    if (IsWindowVisible(edit_) || mode_ == Mode::Text)
        MoveWindow(edit_, content.left + S(4), content.top + S(8), std::max(0L, content.right - content.left - S(4)),
                   std::max(0L, content.bottom - content.top - S(8)), TRUE);
    if (handler_) handler_->SetRect(&content);
    RECT bar = MediaBarRect();
    int b = S(36);
    MoveWindow(playButton_, bar.left + S(16), (bar.top + bar.bottom - b) / 2, b + S(8), b, TRUE);
    RECT videoRect = content;
    videoRect.bottom = bar.top;
    MoveWindow(video_, videoRect.left, videoRect.top, std::max(0L, videoRect.right - videoRect.left),
               std::max(0L, videoRect.bottom - videoRect.top), TRUE);
    if (player_ && hasVideo_) player_->UpdateVideo();
}

void PreviewPane::ApplyEditColors() {
    const Palette& p = Theme::Colors();
    SendMessageW(edit_, EM_SETBKGNDCOLOR, 0, p.surface);
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = p.text;
    SendMessageW(edit_, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
    SendMessageW(edit_, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&cf));
}

void PreviewPane::OnThemeChanged() {
    Theme::ApplyControl(edit_);
    ApplyEditColors();
    if (mode_ == Mode::Handler && !path_.empty()) {
        // Handlers pick up colours only when (re)loaded.
        std::wstring path = path_;
        FileEntry entry = entry_;
        UnloadContent();
        path_.clear();
        ShowFile(path, entry);
    }
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

void PreviewPane::OnFontsChanged() {
    SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(g_fonts.mono), TRUE);
    SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(S(12), S(12)));
    ApplyEditColors();
    Layout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PreviewPane::PaintCentered(HDC hdc, const RECT& area, wchar_t glyph, const std::wstring& title,
                                const std::wstring& subtitle) {
    const Palette& p = Theme::Colors();
    SetBkMode(hdc, TRANSPARENT);
    int width = std::min(static_cast<int>(area.right - area.left) - S(48), S(460));
    if (width <= 0) return;
    int left = (area.left + area.right - width) / 2;

    RECT sub = {left, 0, left + width, 0};
    HGDIOBJ old = SelectObject(hdc, g_fonts.ui);
    int subH = subtitle.empty() ? 0 : DrawTextW(hdc, subtitle.c_str(), -1, &sub, DT_CALCRECT | DT_WORDBREAK | DT_CENTER | DT_NOPREFIX);
    int glyphH = glyph ? S(56) : 0;
    int titleH = title.empty() ? 0 : S(26);
    int total = glyphH + titleH + (subH ? S(6) + subH : 0);
    int y = (area.top + area.bottom - total) / 2;

    if (glyph) {
        RECT gr = {area.left, y, area.right, y + glyphH};
        SelectObject(hdc, g_fonts.iconLarge);
        SetTextColor(hdc, p.textFaint);
        DrawTextW(hdc, &glyph, 1, &gr, DT_SINGLELINE | DT_CENTER | DT_TOP | DT_NOPREFIX);
        y += glyphH;
    }
    if (!title.empty()) {
        RECT tr = {left, y, left + width, y + titleH};
        SelectObject(hdc, g_fonts.title);
        SetTextColor(hdc, p.text);
        DrawTextW(hdc, title.c_str(), -1, &tr, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        y += titleH + S(6);
    }
    if (subH) {
        RECT sr = {left, y, left + width, y + subH};
        SelectObject(hdc, g_fonts.ui);
        SetTextColor(hdc, p.textMuted);
        DrawTextW(hdc, subtitle.c_str(), -1, &sr, DT_WORDBREAK | DT_CENTER | DT_NOPREFIX);
    }
    SelectObject(hdc, old);
}

void PreviewPane::PaintInfoStrip(HDC hdc, const RECT& client) {
    const Palette& p = Theme::Colors();
    RECT strip = {client.left, client.top, client.right, client.top + S(68)};
    Theme::FillRectColor(hdc, strip, p.surface);
    RECT divider = {strip.left, strip.bottom, strip.right, strip.bottom + 1};
    Theme::FillRectColor(hdc, divider, p.border);

    int x = S(16);
    if (fileIcon_) DrawIconEx(hdc, x, strip.top + (S(68) - S(32)) / 2, fileIcon_, S(32), S(32), 0, nullptr, DI_NORMAL);
    x += S(32) + S(14);
    SetBkMode(hdc, TRANSPARENT);

    // Kind pill on the right of the title row.
    int pillRight = strip.right - S(16);
    int pillLeft = pillRight;
    HGDIOBJ old = SelectObject(hdc, g_fonts.small);
    if (!kind_.empty()) {
        SIZE sz{};
        GetTextExtentPoint32W(hdc, kind_.c_str(), static_cast<int>(kind_.size()), &sz);
        pillLeft = pillRight - sz.cx - S(20);
        RECT pill = {pillLeft, strip.top + S(12), pillRight, strip.top + S(12) + S(22)};
        Theme::FillRoundRect(hdc, pill, S(11), p.surfaceAlt, p.border);
        SetTextColor(hdc, p.textMuted);
        DrawTextW(hdc, kind_.c_str(), -1, &pill, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    }

    RECT nameRect = {x, strip.top + S(10), pillLeft - S(12), strip.top + S(36)};
    SelectObject(hdc, g_fonts.title);
    SetTextColor(hdc, p.text);
    DrawTextW(hdc, entry_.name.c_str(), -1, &nameRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);

    std::wstring details = entry_.typeName + L"  ·  " + FormatBytes(entry_.size);
    std::wstring modified = FormatFileTime(entry_.modified);
    if (!modified.empty()) details += L"  ·  Modified " + modified;
    if (!extraInfo_.empty()) details += L"  ·  " + extraInfo_;
    if (entry_.IsHidden()) details += L"  ·  Hidden";
    RECT detailRect = {x, strip.top + S(38), strip.right - S(16), strip.top + S(58)};
    SelectObject(hdc, g_fonts.small);
    SetTextColor(hdc, p.textMuted);
    DrawTextW(hdc, details.c_str(), -1, &detailRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(hdc, old);
}

void PreviewPane::PaintImage(HDC hdc, const RECT& area) {
    if (!image_) return;
    UINT iw = 0, ih = 0;
    image_->GetSize(&iw, &ih);
    int margin = S(16);
    int aw = area.right - area.left - margin * 2;
    int ah = area.bottom - area.top - margin * 2;
    if (aw <= 0 || ah <= 0 || !iw || !ih) return;
    double s = std::min({1.0, static_cast<double>(aw) / iw, static_cast<double>(ah) / ih});
    int w = std::max(1, static_cast<int>(iw * s + 0.5));
    int h = std::max(1, static_cast<int>(ih * s + 0.5));

    if (!scaled_ || scaledSize_.cx != w || scaledSize_.cy != h) {
        if (scaled_) DeleteObject(scaled_);
        scaled_ = nullptr;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;  // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!dib) return;
        ComPtr<IWICBitmapScaler> scaler;
        HRESULT hr = E_FAIL;
        if (static_cast<UINT>(w) == iw && static_cast<UINT>(h) == ih) {
            hr = image_->CopyPixels(nullptr, w * 4, w * 4 * h, static_cast<BYTE*>(bits));
        } else if (SUCCEEDED(Wic()->CreateBitmapScaler(scaler.Put())) &&
                   SUCCEEDED(scaler->Initialize(image_.Get(), w, h, WICBitmapInterpolationModeFant))) {
            hr = scaler->CopyPixels(nullptr, w * 4, w * 4 * h, static_cast<BYTE*>(bits));
        }
        if (FAILED(hr)) {
            DeleteObject(dib);
            return;
        }
        scaled_ = dib;
        scaledSize_ = {w, h};
    }

    int x = area.left + (area.right - area.left - w) / 2;
    int y = area.top + (area.bottom - area.top - h) / 2;
    RECT imgRect = {x, y, x + w, y + h};
    const Palette& p = Theme::Colors();
    if (imageHasAlpha_) {
        Theme::FillRectColor(hdc, imgRect, p.checkerA);
        int cell = S(8);
        HBRUSH b = Theme::Brush(p.checkerB);
        for (int cy = y; cy < y + h; cy += cell)
            for (int cx = x + (((cy - y) / cell) % 2) * cell; cx < x + w; cx += cell * 2) {
                RECT c = {cx, cy, std::min(cx + cell, x + w), std::min(cy + cell, y + h)};
                FillRect(hdc, &c, b);
            }
    }
    HDC mem = CreateCompatibleDC(hdc);
    HGDIOBJ old = SelectObject(mem, scaled_);
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(hdc, x, y, w, h, mem, 0, 0, w, h, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
    RECT frame = imgRect;
    InflateRect(&frame, 1, 1);
    Theme::FrameRectColor(hdc, frame, p.border);
}

void PreviewPane::PaintMediaBar(HDC hdc) {
    const Palette& p = Theme::Colors();
    RECT bar = MediaBarRect();
    Theme::FillRectColor(hdc, bar, p.surfaceAlt);
    RECT line = {bar.left, bar.top, bar.right, bar.top + 1};
    Theme::FillRectColor(hdc, line, p.border);

    int left = bar.left + S(16) + S(44) + S(16);
    int right = bar.right - S(16);
    SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ old = SelectObject(hdc, g_fonts.small);

    if (!mediaError_.empty()) {
        RECT tr = {left, bar.top, right, bar.bottom};
        SetTextColor(hdc, p.danger);
        DrawTextW(hdc, mediaError_.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(hdc, old);
        trackRect_ = {};
        return;
    }

    std::wstring time = player_ ? FormatDuration(positionHns_) + L" / " + (durationHns_ ? FormatDuration(durationHns_) : L"--:--")
                                : L"Press play to preview";
    SIZE sz{};
    GetTextExtentPoint32W(hdc, time.c_str(), static_cast<int>(time.size()), &sz);
    RECT tr = {right - sz.cx, bar.top, right, bar.bottom};
    SetTextColor(hdc, p.textMuted);
    DrawTextW(hdc, time.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
    SelectObject(hdc, old);

    int trackRight = tr.left - S(16);
    int cy = (bar.top + bar.bottom) / 2;
    if (trackRight - left < S(40)) {
        trackRect_ = {};
        return;
    }
    RECT track = {left, cy - S(2), trackRight, cy + S(2)};
    Theme::FillRoundRect(hdc, track, S(2), p.controlBorder, p.controlBorder);
    double frac = durationHns_ > 0 ? std::clamp(static_cast<double>(positionHns_) / durationHns_, 0.0, 1.0) : 0.0;
    int fillX = left + static_cast<int>((trackRight - left) * frac);
    if (fillX > left + 1) {
        RECT filled = {left, track.top, fillX, track.bottom};
        Theme::FillRoundRect(hdc, filled, S(2), p.accent, p.accent);
    }
    if (player_) {
        int r = S(7);
        RECT knob = {fillX - r, cy - r, fillX + r, cy + r};
        HPEN pen = CreatePen(PS_SOLID, 1, p.controlBorder);
        HGDIOBJ op = SelectObject(hdc, pen);
        HGDIOBJ ob = SelectObject(hdc, Theme::Brush(p.control));
        Ellipse(hdc, knob.left, knob.top, knob.right, knob.bottom);
        SelectObject(hdc, Theme::Brush(p.accent));
        SelectObject(hdc, GetStockObject(NULL_PEN));
        int ir = S(4);
        Ellipse(hdc, fillX - ir, cy - ir, fillX + ir + 1, cy + ir + 1);
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(pen);
    }
    trackRect_ = {left, bar.top + S(10), trackRight, bar.bottom - S(10)};
}

void PreviewPane::Paint(HDC target) {
    RECT client;
    GetClientRect(hwnd_, &client);
    if (client.right <= 0 || client.bottom <= 0) return;
    HDC hdc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, client.right, client.bottom);
    HGDIOBJ oldBmp = SelectObject(hdc, bmp);
    const Palette& p = Theme::Colors();
    Theme::FillRectColor(hdc, client, p.surface);

    if (mode_ == Mode::Placeholder && path_.empty()) {
        PaintCentered(hdc, client, placeholderGlyph_, placeholderTitle_, placeholderSubtitle_);
    } else {
        PaintInfoStrip(hdc, client);
        RECT content = ContentRect();
        switch (mode_) {
        case Mode::Placeholder:
            PaintCentered(hdc, content, placeholderGlyph_, placeholderTitle_, placeholderSubtitle_);
            break;
        case Mode::Loading:
            if (!statusText_.empty()) PaintCentered(hdc, content, 0, L"", statusText_);
            break;
        case Mode::Image:
            PaintImage(hdc, content);
            break;
        case Mode::Media: {
            RECT area = content;
            area.bottom = MediaBarRect().top;
            if (!hasVideo_) {
                if (image_)
                    PaintImage(hdc, area);
                else
                    PaintCentered(hdc, area, kind_ == L"Audio" ? static_cast<wchar_t>(0xE8D6) : static_cast<wchar_t>(0xE714),
                                  L"", L"");
            }
            PaintMediaBar(hdc);
            break;
        }
        case Mode::Handler:
        case Mode::Text:
            break;
        }
    }
    BitBlt(target, 0, 0, client.right, client.bottom, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(hdc);
}

// ---------------------------------------------------------------------------------------------
// Window procedures

LRESULT CALLBACK PreviewPane::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PreviewPane* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<PreviewPane*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<PreviewPane*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT PreviewPane::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd_, &ps);
        Paint(hdc);
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_SIZE:
        Layout();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_TIMER:
        if (wp == kLoadTimer) {
            KillTimer(hwnd_, kLoadTimer);
            LoadCurrent();
        } else if (wp == kMediaTimer) {
            UpdateMediaPosition();
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == kPlayButtonId) StartOrTogglePlayback();
        return 0;
    case WM_LBUTTONDOWN: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (mode_ == Mode::Media && PtInRect(&trackRect_, pt)) SeekTo(pt.x);
        return 0;
    }
    case WM_APP_MEDIAEVENT:
        if (HIWORD(wp) == (mediaGeneration_ & 0xFFFF)) OnMediaEvent(LOWORD(wp), static_cast<HRESULT>(lp));
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        const Palette& p = Theme::Colors();
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetTextColor(hdc, p.text);
        SetBkColor(hdc, p.surface);
        return reinterpret_cast<LRESULT>(Theme::Brush(p.surface));
    }
    case WM_DESTROY:
        KillTimer(hwnd_, kLoadTimer);
        UnloadContent();
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

LRESULT CALLBACK PreviewPane::VideoProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    }
    auto* self = reinterpret_cast<PreviewPane*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (self && self->player_ && self->hasVideo_) {
            self->player_->UpdateVideo();
        } else {
            FillRect(hdc, &ps.rcPaint, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        if (self && self->player_ && self->hasVideo_) self->player_->UpdateVideo();
        return 0;
    case WM_LBUTTONUP:
        if (self) self->StartOrTogglePlayback();  // click the video to pause/resume
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
