#ifndef OVERSEER_TEXTURE_PATCH_H
#define OVERSEER_TEXTURE_PATCH_H

#include <stddef.h>
#include <stdint.h>

/* Returns 0 for an unknown map, 1 for the tested original, 2 for the
 * existing palette-patched map. The wrapper only edits textures for 1. */
int overseer_map_kind(const uint8_t *map, size_t size);

/* Called on a populated X1R5G5B5 DirectDraw source surface, before
 * IDirect3DTexture2::Load copies it. kind is 0 for the base surface and 1 for
 * an attached mipmap. Returns the number of pixels changed, or zero when the
 * surface does not match a known asset. */
unsigned overseer_patch_texture(uint8_t *surface, size_t pitch, unsigned width,
                                unsigned height, unsigned kind);

#endif
