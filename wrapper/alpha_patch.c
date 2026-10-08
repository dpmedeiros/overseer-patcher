#include "alpha_patch.h"

unsigned overseer_apply_alpha(uint8_t *pixels, size_t pitch, unsigned width,
                              unsigned height, int keyed, uint16_t key_low,
                              uint16_t key_high) {
  unsigned changed = 0, x, y;
  if (!pixels || pitch < (size_t)width * 2)
    return 0;
  for (y = 0; y < height; ++y) {
    uint16_t *row = (uint16_t *)(pixels + (size_t)y * pitch);
    for (x = 0; x < width; ++x) {
      uint16_t color = row[x] & 0x7fff;
      uint16_t result = color;
      if (!keyed || color < key_low || color > key_high)
        result |= 0x8000;
      changed += row[x] != result;
      row[x] = result;
    }
  }
  return changed;
}
