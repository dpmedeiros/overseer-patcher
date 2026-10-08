#ifndef OVERSEER_ALPHA_PATCH_H
#define OVERSEER_ALPHA_PATCH_H

#include <stddef.h>
#include <stdint.h>

/* Called only for validated A1R5G5B5 texture surfaces. */
unsigned overseer_apply_alpha(uint8_t *pixels, size_t pitch, unsigned width,
                              unsigned height, int keyed, uint16_t key_low,
                              uint16_t key_high);

#endif
