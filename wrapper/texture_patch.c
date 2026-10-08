#include "texture_patch.h"
#include "texture_masks.h"

#define ORIGINAL_MAP_FNV UINT64_C(0x9e2d79370a9d945f)
#define PATCHED_MAP_FNV UINT64_C(0x75aa030863136d04)
#define MAP_SIZE 4887699u
#define FNV_OFFSET UINT64_C(14695981039346656037)
#define FNV_PRIME UINT64_C(1099511628211)

int overseer_map_kind(const uint8_t *map, size_t size) {
  uint64_t hash = FNV_OFFSET;
  size_t i;
  if (!map || size != MAP_SIZE)
    return 0;
  for (i = 0; i < size; ++i) {
    hash ^= map[i];
    hash *= FNV_PRIME;
  }
  if (hash == ORIGINAL_MAP_FNV)
    return 1;
  if (hash == PATCHED_MAP_FNV)
    return 2;
  return 0;
}

static uint64_t surface_hash(const uint8_t *surface, size_t pitch,
                             unsigned width, unsigned height) {
  uint64_t hash = FNV_OFFSET;
  unsigned x, y;
  for (y = 0; y < height; ++y)
    for (x = 0; x < width * 2u; ++x) {
      hash ^= surface[y * pitch + x];
      hash *= FNV_PRIME;
    }
  return hash;
}

struct mask_reader {
  const uint8_t *next;
  const uint8_t *end;
  unsigned remaining;
  unsigned repeat;
  uint8_t value;
};

static int mask_byte(struct mask_reader *reader) {
  if (!reader->remaining) {
    uint8_t control;
    if (reader->next == reader->end)
      return -1;
    control = *reader->next++;
    reader->remaining = (control & 0x7f) + 1u;
    reader->repeat = control & 0x80;
    if (reader->repeat) {
      if (reader->next == reader->end)
        return -1;
      reader->value = *reader->next++;
    }
  }
  --reader->remaining;
  if (reader->repeat)
    return reader->value;
  if (reader->next == reader->end)
    return -1;
  return *reader->next++;
}

static struct mask_reader reader_for(const struct overseer_mask *mask) {
  struct mask_reader reader = {mask->rle, mask->rle + mask->rle_size, 0, 0, 0};
  return reader;
}

static int valid_mask(const struct overseer_mask *mask) {
  struct mask_reader reader = reader_for(mask);
  size_t pixels = (size_t)mask->width * mask->height;
  size_t at;
  unsigned changed = 0;
  for (at = 0; at < (pixels + 7) / 8; ++at) {
    int value = mask_byte(&reader);
    unsigned bit;
    if (value < 0)
      return 0;
    for (bit = 0; bit < 8; ++bit) {
      if ((value & (1 << bit)) && at * 8 + bit >= pixels)
        return 0;
      changed += (value >> bit) & 1;
    }
  }
  return reader.next == reader.end && !reader.remaining &&
         changed == mask->changed_pixels;
}

unsigned overseer_patch_texture(uint8_t *surface, size_t pitch, unsigned width,
                                unsigned height, unsigned kind) {
  size_t i;
  if (!surface || !width || !height || width > 2048 || height > 2048 ||
      pitch < 2u * width)
    return 0;
  for (i = 0; i < overseer_mask_count; ++i) {
    const struct overseer_mask *mask = &overseer_masks[i];
    struct mask_reader reader;
    size_t pixels, byte_pos;
    if (mask->kind != kind || mask->width != width || mask->height != height ||
        surface_hash(surface, pitch, width, height) != mask->original_hash ||
        !valid_mask(mask))
      continue;
    reader = reader_for(mask);
    pixels = (size_t)width * height;
    for (byte_pos = 0; byte_pos < (pixels + 7) / 8; ++byte_pos) {
      int value = mask_byte(&reader);
      unsigned bit;
      for (bit = 0; bit < 8; ++bit) {
        size_t pos = byte_pos * 8 + bit;
        uint8_t *pixel;
        if (pos >= pixels || !(value & (1 << bit)))
          continue;
        pixel = surface + (pos / width) * pitch + (pos % width) * 2;
        pixel[0] = 0x42;
        pixel[1] = 0x08;
      }
    }
    return mask->changed_pixels;
  }
  return 0;
}
