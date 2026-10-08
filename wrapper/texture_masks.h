#ifndef OVERSEER_TEXTURE_MASKS_H
#define OVERSEER_TEXTURE_MASKS_H

#include <stddef.h>
#include <stdint.h>

struct overseer_mask {
  unsigned kind; /* 0 = base surface, 1 = attached mipmap */
  unsigned width;
  unsigned height;
  uint64_t original_hash;
  unsigned changed_pixels;
  const uint8_t *rle;
  size_t rle_size;
};

extern const struct overseer_mask overseer_masks[];
extern const size_t overseer_mask_count;

#endif
