// Preview pane: shows the selected file using the best available renderer.
//
// Order of preference:
//   1. Images decodable by Windows Imaging Component (JPEG, PNG, GIF, BMP, TIFF, ICO, HEIC/WebP/RAW
//      when their codecs are installed), honouring EXIF orientation.
//   2. Audio and video, played in-place with Media Foundation.
//   3. Text and source code (built-in viewer that follows the app theme).
//   4. Registered Windows preview handlers - the same ones Explorer uses (PDF, Word, Excel,
//      PowerPoint, Outlook mail, HTML, SVG, RTF, fonts, and anything third-party apps install).
//   5. Archive listings (ZIP-based formats natively, plus 7z/RAR/TAR where Windows supports them).
//   6. Shell thumbnails from any installed thumbnail provider.
//   7. Hex dump of the first 4 KB for anything else.
#pragma once
#include "Common.h"
#include "FileList.h"

#include <wincodec.h>

struct IMFPMediaPlayer;
class MediaCallback;

class PreviewPane {
public:
    bool Create(HWND parent, int id);
    HWND Hwnd() const { return hwnd_; }

    void ShowFile(const std::wstring& path, const FileEntry& entry);
    void ShowPlaceholder(wchar_t glyph, const std::wstring& title, const std::wstring& subtitle);
    // Drops every handle on the current file (preview handlers and media playback keep files open),
    // leaving a short status message in its place. Call before moving or deleting the file.
    void ReleaseFile(const std::wstring& message);
    void OnThemeChanged();
    void OnFontsChanged();
    const std::wstring& CurrentPath() const { return path_; }

private:
    enum class Mode { Placeholder, Loading, Image, Text, Handler, Media };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK VideoProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

    void LoadCurrent();
    bool TryImage();
    bool TryText(bool requireTextLike);
    bool TryHandler(const CLSID& clsid);
    bool TryZipListing();
    bool TryShellArchive();
    bool TryThumbnail();
    void ShowHex();
    void ShowMediaView();
    bool SetImageFromHBitmap(HBITMAP bitmap);
    void ShowTextContent(const std::wstring& text, const std::wstring& kind);
    void ApplyEditColors();

    void StartOrTogglePlayback();
    void StopMedia();
    void OnMediaEvent(UINT type, HRESULT hr);
    void UpdateMediaPosition();
    void SeekTo(int x);

    void UnloadContent();
    void Layout();
    RECT ContentRect() const;
    RECT MediaBarRect() const;
    int S(int v) const { return ScaleDpi(v, WindowDpi(hwnd_)); }

    void Paint(HDC hdc);
    void PaintInfoStrip(HDC hdc, const RECT& client);
    void PaintImage(HDC hdc, const RECT& area);
    void PaintMediaBar(HDC hdc);
    void PaintCentered(HDC hdc, const RECT& area, wchar_t glyph, const std::wstring& title, const std::wstring& subtitle);

    HWND hwnd_ = nullptr;
    HWND edit_ = nullptr;
    HWND video_ = nullptr;
    HWND playButton_ = nullptr;

    Mode mode_ = Mode::Placeholder;
    std::wstring path_;
    FileEntry entry_;
    std::wstring kind_;        // "Image", "Text", ... shown as a pill in the info strip
    std::wstring extraInfo_;   // e.g. "4032 × 3024"
    std::wstring statusText_;  // loading / working message
    HICON fileIcon_ = nullptr;

    wchar_t placeholderGlyph_ = 0;
    std::wstring placeholderTitle_;
    std::wstring placeholderSubtitle_;

    // Image rendering.
    ComPtr<IWICBitmapSource> image_;
    bool imageHasAlpha_ = false;
    HBITMAP scaled_ = nullptr;
    SIZE scaledSize_{};

    // Shell preview handler.
    ComPtr<IPreviewHandler> handler_;
    ComPtr<IStream> handlerStream_;

    // Media playback.
    IMFPMediaPlayer* player_ = nullptr;
    MediaCallback* callback_ = nullptr;
    UINT mediaGeneration_ = 0;
    bool playing_ = false;
    bool hasVideo_ = false;
    long long durationHns_ = 0;
    long long positionHns_ = 0;
    std::wstring mediaError_;
    RECT trackRect_{};
};
