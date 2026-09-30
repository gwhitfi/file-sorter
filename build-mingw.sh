#!/usr/bin/env bash
# Cross-compiles FileSorter.exe from Linux/WSL with an llvm-mingw or mingw-w64 toolchain.
#   ./build-mingw.sh                      # uses x86_64-w64-mingw32-clang++ (or g++) from PATH
#   CXX=/path/to/x86_64-w64-mingw32-clang++ ./build-mingw.sh
set -euo pipefail
cd "$(dirname "$0")"

CXX="${CXX:-$(command -v x86_64-w64-mingw32-clang++ || command -v x86_64-w64-mingw32-g++)}"
WINDRES="${WINDRES:-$(dirname "$CXX")/x86_64-w64-mingw32-windres}"
mkdir -p build/mingw dist

"$WINDRES" -I res res/app.rc -O coff -o build/mingw/app.res.o
"$CXX" -std=c++20 -O2 -Wall -Wextra -Wno-missing-field-initializers -municode -mwindows \
    -DUNICODE -D_UNICODE -DNOMINMAX -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 \
    src/*.cpp build/mingw/app.res.o -o dist/FileSorter.exe -static -s \
    -lcomctl32 -lshlwapi -lshell32 -lole32 -loleaut32 -luuid -luxtheme -ldwmapi \
    -lwindowscodecs -lmfplat -lmfplay -lmsimg32 -lpropsys -lgdi32 -luser32 -ladvapi32
echo "Built dist/FileSorter.exe"
