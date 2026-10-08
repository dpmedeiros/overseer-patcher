#!/bin/sh
set -eu
cd "$(dirname "$0")"
: "${CC:=clang}"
if [ -z "${WIN32_INCLUDE_DIR:-}" ]; then
    for candidate in /usr/include/wine/windows /usr/include/wine/wine/windows; do
        if [ -f "$candidate/ddraw.h" ]; then
            WIN32_INCLUDE_DIR=$candidate
            break
        fi
    done
fi
if [ -z "${WIN32_LIB_DIR:-}" ]; then
    for candidate in /usr/lib/wine/i386-windows /usr/i686-w64-mingw32/lib /usr/lib/i386-linux-gnu/wine/i386-windows; do
        if [ -f "$candidate/libkernel32.a" ]; then
            WIN32_LIB_DIR=$candidate
            break
        fi
    done
fi
if [ -z "${WIN32_CRT_INCLUDE_DIR:-}" ]; then
    if [ -f /usr/i686-w64-mingw32/include/stdlib.h ]; then
        WIN32_CRT_INCLUDE_DIR=/usr/i686-w64-mingw32/include
    else
        WIN32_CRT_INCLUDE_DIR=/usr/include
    fi
fi
: "${WIN32_INCLUDE_DIR:?Wine Windows headers not found; set WIN32_INCLUDE_DIR}"
: "${WIN32_LIB_DIR:?32-bit Windows import libraries not found; set WIN32_LIB_DIR}"
"$CC" --target=i686-w64-windows-gnu -I"$WIN32_INCLUDE_DIR" -I"$WIN32_CRT_INCLUDE_DIR" \
    -std=c11 -O2 -Wall -Wextra -c alpha_patch.c -o alpha_patch.o
"$CC" --target=i686-w64-windows-gnu -I"$WIN32_INCLUDE_DIR" -I"$WIN32_CRT_INCLUDE_DIR" \
    -std=c11 -O2 -Wall -Wextra -c ddraw_proxy.c -o ddraw_proxy.o
"$CC" --target=i686-w64-windows-gnu -fuse-ld=lld -shared -nostdlib \
    alpha_patch.o ddraw_proxy.o ddraw.def -L"$WIN32_LIB_DIR" \
    -lkernel32 -Wl,--entry,_DllMain@12 -Wl,--no-insert-timestamp -o ddraw.dll
