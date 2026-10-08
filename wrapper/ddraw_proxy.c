/* Overseer-only DirectDraw proxy. Build as 32-bit ddraw.dll. It forwards
 * Overseer's DirectDraw entry points to Wine and corrects known office source
 * textures and mipmaps before IDirect3DTexture2::Load. */
#include "texture_patch.h"
#include <d3d.h>
#include <ddraw.h>
#include <windows.h>

#define MAP_BYTES 4887699u
#define MAX_TEXTURES 4096u

static HMODULE self_module;
static HMODULE system_ddraw;
static int map_kind;
static int map_loaded;

static HRESULT(WINAPI *real_create)(GUID *, IDirectDraw **, IUnknown *);
static HRESULT(WINAPI *real_enumerate_a)(LPDDENUMCALLBACKA, void *);
static HRESULT(WINAPI *real_dd1_qi)(IDirectDraw *, REFIID, void **);
static HRESULT(WINAPI *real_dd1_surface)(IDirectDraw *, DDSURFACEDESC *,
                                         IDirectDrawSurface **, IUnknown *);
static HRESULT(WINAPI *real_dd2_qi)(IDirectDraw2 *, REFIID, void **);
static HRESULT(WINAPI *real_dd2_surface)(IDirectDraw2 *, DDSURFACEDESC *,
                                         IDirectDrawSurface **, IUnknown *);
static HRESULT(WINAPI *real_surface_qi)(IDirectDrawSurface *, REFIID, void **);
static HRESULT(WINAPI *real_texture_load)(IDirect3DTexture2 *,
                                          IDirect3DTexture2 *);

struct texture_surface {
  IDirect3DTexture2 *texture;
  IDirectDrawSurface *surface;
};
static struct texture_surface textures[MAX_TEXTURES];
static unsigned texture_count;
static const GUID texture2_iid = {
    0x93281502,
    0x8cf8,
    0x11d0,
    {0x89, 0xab, 0x00, 0xa0, 0xc9, 0x05, 0x41, 0x29}};
static const GUID dd2_iid = {0xb3a6f3e0,
                             0x2b43,
                             0x11cf,
                             {0xa2, 0xde, 0x00, 0xaa, 0x00, 0xb9, 0x33, 0x56}};

static int same_guid(REFIID a, const GUID *b) {
  const uint8_t *pa = (const uint8_t *)a, *pb = (const uint8_t *)b;
  unsigned i;
  for (i = 0; i < sizeof(GUID); ++i)
    if (pa[i] != pb[i])
      return 0;
  return 1;
}

static void trace_number(const char *label, unsigned value) {
  char buf[96];
  const char hex[] = "0123456789abcdef";
  unsigned i = 0, j;
  while (label[i] && i < 70) {
    buf[i] = label[i];
    ++i;
  }
  for (j = 0; j < 8; ++j)
    buf[i++] = hex[(value >> (28 - 4 * j)) & 15];
  buf[i++] = '\n';
  buf[i] = 0;
  OutputDebugStringA(buf);
}

static int replace_slot(void **slot, void *replacement, void **original) {
  DWORD protection, ignored;
  if (*slot == replacement)
    return 1;
  if (!VirtualProtect(slot, sizeof(void *), PAGE_EXECUTE_READWRITE,
                      &protection))
    return 0;
  if (!*original)
    *original = *slot;
  *slot = replacement;
  VirtualProtect(slot, sizeof(void *), protection, &ignored);
  return 1;
}

static void load_map(void) {
  char path[MAX_PATH];
  DWORD length, size, read_bytes;
  HANDLE file;
  uint8_t *map_data;
  unsigned i;
  if (map_loaded)
    return;
  map_loaded = 1;
  length = GetModuleFileNameA(self_module, path, MAX_PATH);
  if (!length || length >= MAX_PATH)
    return;
  while (length && path[length - 1] != '\\' && path[length - 1] != '/')
    --length;
  if (!length || length + sizeof("DATA\\R01\\R01.MAP") >= MAX_PATH)
    return;
  for (i = 0; i < sizeof("DATA\\R01\\R01.MAP"); ++i)
    path[length + i] = "DATA\\R01\\R01.MAP"[i];
  file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                     NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (file == INVALID_HANDLE_VALUE)
    return;
  size = GetFileSize(file, NULL);
  if (size == MAP_BYTES) {
    map_data = HeapAlloc(GetProcessHeap(), 0, size);
    if (map_data && ReadFile(file, map_data, size, &read_bytes, NULL) &&
        read_bytes == size)
      map_kind = overseer_map_kind(map_data, size);
    if (map_data)
      HeapFree(GetProcessHeap(), 0, map_data);
  }
  CloseHandle(file);
  trace_number("Overseer map kind: ", (unsigned)map_kind);
}

static void patch_surface(IDirectDrawSurface *surface) {
  DDPIXELFORMAT format = {0};
  DDSURFACEDESC desc = {0};
  unsigned changed;
  if (!surface || map_kind != 1)
    return;
  format.dwSize = sizeof(format);
  if (FAILED(IDirectDrawSurface_GetPixelFormat(surface, &format)) ||
      format.dwRGBBitCount != 16 || format.dwRBitMask != 0x7c00 ||
      format.dwGBitMask != 0x03e0 || format.dwBBitMask != 0x001f)
    return;
  desc.dwSize = sizeof(desc);
  if (FAILED(IDirectDrawSurface_Lock(surface, NULL, &desc, DDLOCK_WAIT, NULL)))
    return;
  if (desc.lPitch > 0 && desc.lpSurface) {
    changed = overseer_patch_texture((uint8_t *)desc.lpSurface,
                                     (size_t)desc.lPitch, desc.dwWidth,
                                     desc.dwHeight, 0);
    if (changed)
      trace_number("Overseer pixels corrected: ", changed);
  }
  IDirectDrawSurface_Unlock(surface, desc.lpSurface);
}

static void record_texture(IDirect3DTexture2 *texture,
                           IDirectDrawSurface *surface) {
  unsigned i;
  for (i = 0; i < texture_count; ++i) {
    if (textures[i].texture == texture) {
      textures[i].surface = surface;
      return;
    }
  }
  if (texture_count < MAX_TEXTURES) {
    textures[texture_count].texture = texture;
    textures[texture_count].surface = surface;
    ++texture_count;
  }
}

static IDirectDrawSurface *find_surface(IDirect3DTexture2 *texture) {
  unsigned i;
  for (i = texture_count; i > 0; --i)
    if (textures[i - 1].texture == texture)
      return textures[i - 1].surface;
  return NULL;
}

static void patch_mips(IDirectDrawSurface *base) {
  IDirectDrawSurface *current = base, *next;
  DDSCAPS caps = {DDSCAPS_MIPMAP};
  DDSURFACEDESC desc = {0};
  unsigned level;
  if (!base || map_kind != 1)
    return;
  for (level = 1; level <= 10; ++level) {
    next = NULL;
    if (FAILED(IDirectDrawSurface_GetAttachedSurface(current, &caps, &next)) ||
        !next || next == current)
      break;
    desc.dwSize = sizeof(desc);
    if (SUCCEEDED(IDirectDrawSurface_Lock(next, NULL, &desc, DDLOCK_WAIT, NULL))) {
      unsigned changed = 0;
      if (desc.lPitch > 0 && desc.lpSurface)
        changed = overseer_patch_texture(
            (uint8_t *)desc.lpSurface, (size_t)desc.lPitch, desc.dwWidth,
            desc.dwHeight, 1);
      if (changed)
        trace_number("Overseer mip pixels corrected: ", changed);
      IDirectDrawSurface_Unlock(next, desc.lpSurface);
    }
    if (current != base)
      IDirectDrawSurface_Release(current);
    current = next;
  }
  if (current != base)
    IDirectDrawSurface_Release(current);
}

static HRESULT WINAPI texture_load(IDirect3DTexture2 *target,
                                   IDirect3DTexture2 *source) {
  patch_surface(find_surface(source));
  patch_mips(find_surface(source));
  return real_texture_load(target, source);
}

static void hook_texture(IDirect3DTexture2 *texture) {
  if (texture)
    replace_slot((void **)&texture->lpVtbl->Load, (void *)texture_load,
                 (void **)&real_texture_load);
}

static HRESULT WINAPI surface_qi(IDirectDrawSurface *surface, REFIID iid,
                                 void **out) {
  HRESULT hr = real_surface_qi(surface, iid, out);
  if (SUCCEEDED(hr) && out && *out && same_guid(iid, &texture2_iid)) {
    record_texture((IDirect3DTexture2 *)*out, surface);
    hook_texture((IDirect3DTexture2 *)*out);
  }
  return hr;
}

static void hook_surface(IDirectDrawSurface *surface) {
  if (surface)
    replace_slot((void **)&surface->lpVtbl->QueryInterface, (void *)surface_qi,
                 (void **)&real_surface_qi);
}

static HRESULT WINAPI dd1_surface(IDirectDraw *dd, DDSURFACEDESC *desc,
                                  IDirectDrawSurface **surface,
                                  IUnknown *outer) {
  HRESULT hr = real_dd1_surface(dd, desc, surface, outer);
  if (SUCCEEDED(hr) && surface)
    hook_surface(*surface);
  return hr;
}

static HRESULT WINAPI dd2_surface(IDirectDraw2 *dd, DDSURFACEDESC *desc,
                                  IDirectDrawSurface **surface,
                                  IUnknown *outer) {
  HRESULT hr = real_dd2_surface(dd, desc, surface, outer);
  if (SUCCEEDED(hr) && surface)
    hook_surface(*surface);
  return hr;
}

static void hook_dd2(IDirectDraw2 *dd);

static HRESULT WINAPI dd1_qi(IDirectDraw *dd, REFIID iid, void **out) {
  HRESULT hr = real_dd1_qi(dd, iid, out);
  if (SUCCEEDED(hr) && out && *out && same_guid(iid, &dd2_iid))
    hook_dd2((IDirectDraw2 *)*out);
  return hr;
}

static HRESULT WINAPI dd2_qi(IDirectDraw2 *dd, REFIID iid, void **out) {
  HRESULT hr = real_dd2_qi(dd, iid, out);
  if (SUCCEEDED(hr) && out && *out && same_guid(iid, &dd2_iid))
    hook_dd2((IDirectDraw2 *)*out);
  return hr;
}

static void hook_dd2(IDirectDraw2 *dd) {
  replace_slot((void **)&dd->lpVtbl->QueryInterface, (void *)dd2_qi,
               (void **)&real_dd2_qi);
  replace_slot((void **)&dd->lpVtbl->CreateSurface, (void *)dd2_surface,
               (void **)&real_dd2_surface);
}

static void hook_dd1(IDirectDraw *dd) {
  replace_slot((void **)&dd->lpVtbl->QueryInterface, (void *)dd1_qi,
               (void **)&real_dd1_qi);
  replace_slot((void **)&dd->lpVtbl->CreateSurface, (void *)dd1_surface,
               (void **)&real_dd1_surface);
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) {
  (void)reserved;
  if (reason == DLL_PROCESS_ATTACH)
    self_module = module;
  return TRUE;
}

static int load_system_ddraw(void) {
  char path[MAX_PATH];
  DWORD length;
  if (system_ddraw)
    return 1;
  length = GetSystemDirectoryA(path, MAX_PATH);
  if (!length || length + sizeof("\\ddraw.dll") >= MAX_PATH)
    return 0;
  path[length++] = '\\';
  path[length++] = 'd';
  path[length++] = 'd';
  path[length++] = 'r';
  path[length++] = 'a';
  path[length++] = 'w';
  path[length++] = '.';
  path[length++] = 'd';
  path[length++] = 'l';
  path[length++] = 'l';
  path[length] = 0;
  system_ddraw = LoadLibraryA(path);
  return system_ddraw && system_ddraw != self_module;
}

HRESULT WINAPI DirectDrawEnumerateA(LPDDENUMCALLBACKA callback, void *context) {
  if (!load_system_ddraw())
    return E_FAIL;
  if (!real_enumerate_a)
    real_enumerate_a =
        (void *)GetProcAddress(system_ddraw, "DirectDrawEnumerateA");
  if (!real_enumerate_a)
    return E_FAIL;
  return real_enumerate_a(callback, context);
}

HRESULT WINAPI DirectDrawCreate(GUID *guid, IDirectDraw **out,
                                IUnknown *outer) {
  HRESULT hr;
  if (!load_system_ddraw())
    return E_FAIL;
  if (!real_create) {
    real_create = (void *)GetProcAddress(system_ddraw, "DirectDrawCreate");
    if (!real_create)
      return E_FAIL;
    load_map();
  }
  hr = real_create(guid, out, outer);
  if (SUCCEEDED(hr) && out && *out)
    hook_dd1(*out);
  return hr;
}
