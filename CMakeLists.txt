cmake_minimum_required(VERSION 3.20)
project(FileSorter VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(FileSorter WIN32
    src/main.cpp
    src/App.cpp
    src/Button.cpp
    src/DestinationDialog.cpp
    src/FileList.cpp
    src/PreviewPane.cpp
    src/Settings.cpp
    src/Theme.cpp
    res/app.rc
)

target_compile_definitions(FileSorter PRIVATE UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)

target_link_libraries(FileSorter PRIVATE
    comctl32 shlwapi shell32 ole32 oleaut32 uuid uxtheme dwmapi
    windowscodecs mfplat mfplay msimg32 propsys gdi32 user32 advapi32)

if(MSVC)
    target_compile_options(FileSorter PRIVATE /W4 /utf-8 /permissive-)
    # The manifest is embedded through app.rc.
    target_link_options(FileSorter PRIVATE /MANIFEST:NO)
    # Static CRT: the .exe runs on a clean Windows 10/11 machine without the VC++ redistributable.
    set_property(TARGET FileSorter PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(FileSorter PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
    target_link_options(FileSorter PRIVATE -municode -static)
endif()
