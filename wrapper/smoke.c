#include <d3d.h>
#include <ddraw.h>
#include <windows.h>
void *memcpy(void *d, const void *s, size_t n) {
  char *p = d;
  const char *q = s;
  while (n--)
    *p++ = *q++;
  return d;
}
void *memset(void *d, int v, size_t n) {
  char *p = d;
  while (n--)
    *p++ = (char)v;
  return d;
}
void __main(void) {}
static const GUID dd2 = {
    0xb3a6f3e0, 0x2b43, 0x11cf, {0xa2, 0xde, 0, 0xaa, 0, 0xb9, 0x33, 0x56}};
static const GUID tex2 = {
    0x93281502, 0x8cf8, 0x11d0, {0x89, 0xab, 0, 0xa0, 0xc9, 5, 0x41, 0x29}};
static unsigned u32(const unsigned char *p) {
  return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
}
static int check(HRESULT hr, const char *msg) {
  if (FAILED(hr)) {
    OutputDebugStringA(msg);
    return 0;
  }
  return 1;
}
int main(void) {
  HMODULE dll = LoadLibraryA("ddraw.dll");
  HRESULT(WINAPI * create)(GUID *, IDirectDraw **, IUnknown *) =
      (void *)GetProcAddress(dll, "DirectDrawCreate");
  IDirectDraw *dd = 0;
  IDirectDraw2 *ddraw2 = 0;
  IDirectDrawSurface *src = 0, *dst = 0;
  IDirect3DTexture2 *st = 0, *dt = 0;
  DDSURFACEDESC sd = {0};
  DDSURFACEDESC lock = {0};
  DDCOLORKEY key = {0, 0};
  HANDLE file;
  unsigned char *map = 0;
  DWORD got = 0;
  unsigned e, pal, pix, x, y, changed = 0;
  if (!create || !check(create(0, &dd, 0), "create failed"))
    return 1;
  OutputDebugStringA("after create");
  if (!check(IDirectDraw_QueryInterface(dd, &dd2, (void **)&ddraw2),
             "qi dd2 failed"))
    return 2;
  OutputDebugStringA("after qi dd2");
  if (!check(IDirectDraw2_SetCooperativeLevel(ddraw2, 0, DDSCL_NORMAL),
             "cooperative failed"))
    return 3;
  OutputDebugStringA("after cooperative");
  sd.dwSize = sizeof(sd);
  sd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
  sd.dwWidth = 256;
  sd.dwHeight = 128;
  sd.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY;
  sd.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
  sd.ddpfPixelFormat.dwFlags = DDPF_RGB;
  sd.ddpfPixelFormat.dwRGBBitCount = 16;
  sd.ddpfPixelFormat.dwRBitMask = 0x7c00;
  sd.ddpfPixelFormat.dwGBitMask = 0x03e0;
  sd.ddpfPixelFormat.dwBBitMask = 0x001f;
  if (!check(IDirectDraw2_CreateSurface(ddraw2, &sd, &src, 0),
             "src surface failed"))
    return 4;
  OutputDebugStringA("after src surface");
  if (!check(IDirectDraw2_CreateSurface(ddraw2, &sd, &dst, 0),
             "dst surface failed"))
    return 5;
  OutputDebugStringA("after dst surface");
  if (!check(IDirectDrawSurface_QueryInterface(src, &tex2, (void **)&st),
             "src tex failed"))
    return 6;
  OutputDebugStringA("after src tex");
  if (!check(IDirectDrawSurface_QueryInterface(dst, &tex2, (void **)&dt),
             "dst tex failed"))
    return 7;
  OutputDebugStringA("after dst tex");
  if (!check(IDirectDrawSurface_SetColorKey(src, DDCKEY_SRCBLT, &key),
             "color key failed"))
    return 14;
  file = CreateFileA("DATA\\R01\\R01.MAP", GENERIC_READ, FILE_SHARE_READ, 0,
                     OPEN_EXISTING, 0, 0);
  if (file == INVALID_HANDLE_VALUE)
    return 8;
  map = HeapAlloc(GetProcessHeap(), 0, 4887699);
  if (!ReadFile(file, map, 4887699, &got, 0) || got != 4887699)
    return 9;
  CloseHandle(file);
  e = u32(map + 12) + 36 * 27;
  pal = u32(map + e + 28);
  pix = u32(map + e + 32);
  lock.dwSize = sizeof(lock);
  if (!check(IDirectDrawSurface_Lock(src, 0, &lock, DDLOCK_WAIT, 0),
             "lock failed"))
    return 10;
  OutputDebugStringA("after lock");
  for (y = 0; y < 128; y++)
    for (x = 0; x < 256; x++) {
      unsigned i = map[pix + y * 256 + x];
      unsigned char *c = map + pal + 4 * i;
      unsigned short rgb =
          ((c[0] >> 3) << 10) | ((c[1] >> 3) << 5) | (c[2] >> 3);
      *(unsigned short *)((unsigned char *)lock.lpSurface + y * lock.lPitch +
                          x * 2) = rgb;
    }
  IDirectDrawSurface_Unlock(src, lock.lpSurface);
  OutputDebugStringA("after unlock");
  if (!check(IDirect3DTexture2_Load(dt, st), "load failed"))
    return 11;
  OutputDebugStringA("after texture load");
  lock.dwSize = sizeof(lock);
  if (!check(IDirectDrawSurface_Lock(src, 0, &lock, DDLOCK_WAIT, 0),
             "relock failed"))
    return 12;
  for (y = 0; y < 128; y++)
    for (x = 0; x < 256; x++)
      if (map[pix + y * 256 + x] == 24) {
        unsigned short rgb =
            *(unsigned short *)((unsigned char *)lock.lpSurface +
                                y * lock.lPitch + x * 2);
        if (rgb == 0x421)
          changed++;
      }
  IDirectDrawSurface_Unlock(src, lock.lpSurface);
  if (changed == 452) {
    OutputDebugStringA("surface smoke pass");
    return 0;
  }
  OutputDebugStringA("surface smoke mismatch");
  return 13;
}
