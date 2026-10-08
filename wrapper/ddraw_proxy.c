/* Overseer DirectDraw proxy. It selects A1R5G5B5 textures and sets alpha
 * from each source surface's color key before Direct3D copies the texture. */
#include "alpha_patch.h"
#include <d3d.h>
#include <ddraw.h>
#include <windows.h>

#define MAX_TEXTURES 32768u

static HMODULE self_module;
static HMODULE system_ddraw;

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
static ULONG(WINAPI *real_texture_release)(IDirect3DTexture2 *);
static HRESULT(WINAPI *real_d3d2_create_device)(IDirect3D2 *, REFCLSID,
                                                IDirectDrawSurface *,
                                                IDirect3DDevice2 **);
static HRESULT(WINAPI *real_device2_enum_formats)(
    IDirect3DDevice2 *, LPD3DENUMTEXTUREFORMATSCALLBACK, void *);

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
static const GUID d3d2_iid = {0x6aae1ec1,
                              0x662a,
                              0x11d0,
                              {0x88, 0x9d, 0x00, 0xaa, 0x00, 0xbb, 0xb7, 0x6a}};

static int same_guid(REFIID a, const GUID *b) {
  const uint8_t *pa = (const uint8_t *)a, *pb = (const uint8_t *)b;
  unsigned i;
  for (i = 0; i < sizeof(GUID); ++i)
    if (pa[i] != pb[i])
      return 0;
  return 1;
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

static void prepare_alpha_surface(IDirectDrawSurface *surface) {
  DDPIXELFORMAT format = {0};
  DDSURFACEDESC desc = {0};
  DDCOLORKEY key = {0};
  int keyed;
  if (!surface)
    return;
  format.dwSize = sizeof(format);
  if (FAILED(IDirectDrawSurface_GetPixelFormat(surface, &format)) ||
      !(format.dwFlags & DDPF_ALPHAPIXELS) ||
      format.dwRGBAlphaBitMask != 0x8000 ||
      format.dwRGBBitCount != 16 || format.dwRBitMask != 0x7c00 ||
      format.dwGBitMask != 0x03e0 || format.dwBBitMask != 0x001f)
    return;
  keyed = SUCCEEDED(IDirectDrawSurface_GetColorKey(surface, DDCKEY_SRCBLT,
                                                    &key));
  desc.dwSize = sizeof(desc);
  if (FAILED(IDirectDrawSurface_Lock(surface, NULL, &desc, DDLOCK_WAIT, NULL)))
    return;
  if (desc.lPitch > 0 && desc.lpSurface)
    overseer_apply_alpha((uint8_t *)desc.lpSurface, (size_t)desc.lPitch,
                         desc.dwWidth, desc.dwHeight, keyed,
                         (uint16_t)key.dwColorSpaceLowValue,
                         (uint16_t)key.dwColorSpaceHighValue);
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

static void prepare_mips(IDirectDrawSurface *base) {
  IDirectDrawSurface *current = base, *next;
  DDSCAPS caps = {DDSCAPS_MIPMAP};
  unsigned level;
  if (!base)
    return;
  for (level = 1; level <= 16; ++level) {
    next = NULL;
    if (FAILED(IDirectDrawSurface_GetAttachedSurface(current, &caps, &next)) ||
        !next || next == current)
      break;
    prepare_alpha_surface(next);
    if (current != base)
      IDirectDrawSurface_Release(current);
    current = next;
  }
  if (current != base)
    IDirectDrawSurface_Release(current);
}

static HRESULT WINAPI texture_load(IDirect3DTexture2 *target,
                                   IDirect3DTexture2 *source) {
  IDirectDrawSurface *surface = find_surface(source);
  prepare_alpha_surface(surface);
  prepare_mips(surface);
  return real_texture_load(target, source);
}

static ULONG WINAPI texture_release(IDirect3DTexture2 *texture) {
  ULONG references = real_texture_release(texture);
  unsigned i;
  if (!references)
    for (i = 0; i < texture_count; ++i)
      if (textures[i].texture == texture) {
        textures[i] = textures[--texture_count];
        break;
      }
  return references;
}

static void hook_texture(IDirect3DTexture2 *texture) {
  if (texture)
    replace_slot((void **)&texture->lpVtbl->Load, (void *)texture_load,
                 (void **)&real_texture_load);
  if (texture)
    replace_slot((void **)&texture->lpVtbl->Release, (void *)texture_release,
                 (void **)&real_texture_release);
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

struct format_callback {
  LPD3DENUMTEXTUREFORMATSCALLBACK original;
  void *context;
  int found;
};

static int is_a1r5g5b5(const DDSURFACEDESC *desc) {
  const DDPIXELFORMAT *format = &desc->ddpfPixelFormat;
  return (format->dwFlags & (DDPF_RGB | DDPF_ALPHAPIXELS)) ==
             (DDPF_RGB | DDPF_ALPHAPIXELS) &&
         format->dwRGBBitCount == 16 && format->dwRBitMask == 0x7c00 &&
         format->dwGBitMask == 0x03e0 && format->dwBBitMask == 0x001f &&
         format->dwRGBAlphaBitMask == 0x8000;
}

static HRESULT CALLBACK find_alpha_format(DDSURFACEDESC *desc, void *context) {
  struct format_callback *callback = context;
  callback->found |= is_a1r5g5b5(desc);
  return D3DENUMRET_OK;
}

static HRESULT CALLBACK forward_alpha_format(DDSURFACEDESC *desc,
                                             void *context) {
  struct format_callback *callback = context;
  if (!is_a1r5g5b5(desc))
    return D3DENUMRET_OK;
  return callback->original(desc, callback->context);
}

static HRESULT WINAPI device2_enum_formats(
    IDirect3DDevice2 *device, LPD3DENUMTEXTUREFORMATSCALLBACK callback,
    void *context) {
  struct format_callback wrapped = {callback, context, 0};
  HRESULT hr = real_device2_enum_formats(device, find_alpha_format, &wrapped);
  if (FAILED(hr) || !wrapped.found)
    return real_device2_enum_formats(device, callback, context);
  return real_device2_enum_formats(device, forward_alpha_format, &wrapped);
}

static HRESULT WINAPI d3d2_create_device(IDirect3D2 *d3d, REFCLSID clsid,
                                         IDirectDrawSurface *surface,
                                         IDirect3DDevice2 **device) {
  HRESULT hr = real_d3d2_create_device(d3d, clsid, surface, device);
  if (SUCCEEDED(hr) && device && *device)
    replace_slot((void **)&(*device)->lpVtbl->EnumTextureFormats,
                 (void *)device2_enum_formats,
                 (void **)&real_device2_enum_formats);
  return hr;
}

static void hook_d3d2(IDirect3D2 *d3d) {
  replace_slot((void **)&d3d->lpVtbl->CreateDevice,
               (void *)d3d2_create_device,
               (void **)&real_d3d2_create_device);
}

static HRESULT WINAPI dd1_qi(IDirectDraw *dd, REFIID iid, void **out) {
  HRESULT hr = real_dd1_qi(dd, iid, out);
  if (SUCCEEDED(hr) && out && *out && same_guid(iid, &dd2_iid))
    hook_dd2((IDirectDraw2 *)*out);
  if (SUCCEEDED(hr) && out && *out && same_guid(iid, &d3d2_iid))
    hook_d3d2((IDirect3D2 *)*out);
  return hr;
}

static HRESULT WINAPI dd2_qi(IDirectDraw2 *dd, REFIID iid, void **out) {
  HRESULT hr = real_dd2_qi(dd, iid, out);
  if (SUCCEEDED(hr) && out && *out && same_guid(iid, &dd2_iid))
    hook_dd2((IDirectDraw2 *)*out);
  if (SUCCEEDED(hr) && out && *out && same_guid(iid, &d3d2_iid))
    hook_d3d2((IDirect3D2 *)*out);
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
  }
  hr = real_create(guid, out, outer);
  if (SUCCEEDED(hr) && out && *out)
    hook_dd1(*out);
  return hr;
}
