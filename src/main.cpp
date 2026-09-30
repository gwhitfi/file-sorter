// File Sorter - entry point.
#include "App.h"

#include <mfapi.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    // The manifest requests per-monitor DPI awareness; this covers builds where it wasn't embedded.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    if (FAILED(OleInitialize(nullptr))) return 1;
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
    bool mediaFoundation = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL));

    // Optional command-line argument: a folder to open (or a file, which opens its folder and selects it).
    std::wstring initial;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        if (argc > 1) initial = argv[1];
        LocalFree(argv);
    }

    int exitCode = 1;
    {
        App app;
        if (app.Create(instance, showCommand, initial)) exitCode = app.Run();
    }

    if (mediaFoundation) MFShutdown();
    OleUninitialize();
    return exitCode;
}
