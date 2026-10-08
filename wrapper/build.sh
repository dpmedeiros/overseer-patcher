#!/bin/sh
set -eu
cd "$(dirname "$0")"
: "${CC:=clang}"
"$CC" --target=i686-w64-windows-gnu -I/usr/include/wine/windows -I/usr/include \
    -std=c11 -O2 -Wall -Wextra -c texture_patch.c -o texture_patch.o
"$CC" --target=i686-w64-windows-gnu -I/usr/include/wine/windows -I/usr/include \
    -std=c11 -O2 -Wall -Wextra -c ddraw_proxy.c -o ddraw_proxy.o
"$CC" --target=i686-w64-windows-gnu -fuse-ld=lld -shared -nostdlib \
    texture_patch.o ddraw_proxy.o ddraw.def -L/usr/lib/wine/i386-windows \
    -lkernel32 -Wl,--entry,_DllMain@12 -o ddraw.dll
if [ "${1:-}" = "--smoke" ]; then
    "$CC" --target=i686-w64-windows-gnu -I/usr/include/wine/windows -I/usr/include \
        -O0 -c smoke.c -o smoke.o
    "$CC" --target=i686-w64-windows-gnu -fuse-ld=lld -nostdlib \
        smoke.o -L/usr/lib/wine/i386-windows -lkernel32 \
        -Wl,--entry,_main -o smoke.exe
fi
