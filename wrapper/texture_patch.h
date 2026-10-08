#ifndef OVERSEER_TEXTURE_PATCH_H
#define OVERSEER_TEXTURE_PATCH_H

#include <stddef.h>
#include <stdint.h>

/* Returns 0 for an unknown map, 1 for the tested original, 2 for the
 * existing palette-patched map. The wrapper only edits textures for 1. */
int overseer_map_kind(const uint8_t *map, size_t size);

/* Called on a populated X1R5G5B5 DirectDraw source surface, before
 * IDirect3DTexture2::Load copies it. Returns the number of pixels changed.
 * A return value of zero also means the surface did not match a known asset. */
unsigned overseer_patch_texture(const uint8_t *map, size_t map_size,
                                uint8_t *surface, size_t pitch, unsigned width,
                                unsigned height);

#endif
