#include "texture_patch.h"

#define ORIGINAL_MAP_FNV UINT64_C(0x9e2d79370a9d945f)
#define PATCHED_MAP_FNV UINT64_C(0x75aa030863136d04)
#define MAP_SIZE 4887699u

struct target {
  unsigned texture;
  const uint8_t *indices;
  unsigned count;
};

static const uint8_t i24[] = {24};
static const uint8_t i29[] = {29};
static const uint8_t i33[] = {31, 32, 34, 35, 36, 37, 38};
static const uint8_t i34[] = {31, 35, 36, 37, 38};
static const uint8_t i35[] = {31, 36, 37, 38};
static const uint8_t i37[] = {37};
static const uint8_t i198[] = {42, 43, 44, 45, 46, 47, 48, 49, 50, 51};
static const uint8_t i199[] = {44, 48, 49, 51};
static const uint8_t i201[] = {128, 129, 130, 131};
static const uint8_t i204[] = {102, 103, 104, 105, 106, 107, 108};

#define TARGET(n, a) {n, a, sizeof(a)}
static const struct target targets[] = {
    TARGET(27, i24),   TARGET(28, i24),   TARGET(29, i24),   TARGET(30, i29),
    TARGET(31, i29),   TARGET(32, i29),   TARGET(33, i33),   TARGET(34, i34),
    TARGET(35, i35),   TARGET(36, i37),   TARGET(37, i37),   TARGET(38, i37),
    TARGET(198, i198), TARGET(199, i199), TARGET(200, i199), TARGET(201, i201),
    TARGET(202, i201), TARGET(203, i201), TARGET(204, i204), TARGET(205, i204),
    TARGET(206, i204),
};

static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}

static uint16_t le16(const uint8_t *p) {
  return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static void put16(uint8_t *p, uint16_t value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
}

int overseer_map_kind(const uint8_t *map, size_t size) {
  uint64_t hash = UINT64_C(14695981039346656037);
  size_t i;
  if (!map || size != MAP_SIZE)
    return 0;
  for (i = 0; i < size; ++i) {
    hash ^= map[i];
    hash *= UINT64_C(1099511628211);
  }
  if (hash == ORIGINAL_MAP_FNV)
    return 1;
  if (hash == PATCHED_MAP_FNV)
    return 2;
  return 0;
}

static int selected(const struct target *target, uint8_t index) {
  unsigned i;
  for (i = 0; i < target->count; ++i)
    if (target->indices[i] == index)
      return 1;
  return 0;
}

static uint16_t rgb555(const uint8_t *palette, uint8_t index, int swap) {
  const uint8_t *color = palette + 4u * index;
  unsigned r = color[swap ? 2 : 0] >> 3;
  unsigned g = color[1] >> 3;
  unsigned b = color[swap ? 0 : 2] >> 3;
  return (uint16_t)((r << 10) | (g << 5) | b);
}

static int texture_layout(const uint8_t *map, size_t map_size, unsigned texture,
                          unsigned width, unsigned height,
                          const uint8_t **palette, const uint8_t **pixels) {
  size_t metadata, entry, pal, pix, count;
  if (map_size < 16 || le32(map + 8) != 331)
    return 0;
  metadata = le32(map + 12);
  entry = metadata + 36u * texture;
  if (entry > map_size || map_size - entry < 36 || le32(map + entry) != width ||
      le32(map + entry + 4) != height || le32(map + entry + 8) != 8)
    return 0;
  pal = le32(map + entry + 28);
  pix = le32(map + entry + 32);
  count = (size_t)width * height;
  if (pal > map_size || map_size - pal < 1024 || pix != pal + 1024 ||
      pix > map_size || map_size - pix < count)
    return 0;
  *palette = map + pal;
  *pixels = map + pix;
  return 1;
}

/* Require multiple exact, non-key samples. Sampling ignores source black,
 * because 5:5:5 conversion has already lost distinctions among near-black
 * palette entries. That distinction is recovered from the original map. */
static int matches(const struct target *target, const uint8_t *palette,
                   const uint8_t *pixels, const uint8_t *surface, size_t pitch,
                   unsigned width, unsigned height, int flipped, int swapped) {
  size_t count = (size_t)width * height;
  unsigned good = 0, attempt;
  for (attempt = 0; attempt < 256; ++attempt) {
    size_t pos = ((size_t)attempt * 131071u + 97u) % count;
    unsigned x = (unsigned)(pos % width);
    unsigned y = (unsigned)(pos / width);
    uint8_t index = pixels[pos];
    uint16_t expected = rgb555(palette, index, swapped);
    uint16_t actual;
    if (!expected || selected(target, index))
      continue;
    actual = le16(surface + (flipped ? height - 1u - y : y) * pitch + 2u * x);
    if ((actual & 0x7fff) != expected)
      return 0;
    ++good;
  }
  return good >= (count < 1024 ? 8u : 24u);
}

unsigned overseer_patch_texture(const uint8_t *map, size_t map_size,
                                uint8_t *surface, size_t pitch, unsigned width,
                                unsigned height) {
  unsigned t;
  if (!map || !surface || !width || !height || width > 2048 || height > 2048 ||
      pitch < 2u * width)
    return 0;
  for (t = 0; t < sizeof(targets) / sizeof(targets[0]); ++t) {
    const struct target *target = &targets[t];
    const uint8_t *palette, *pixels;
    int flipped, swapped;
    size_t pos, count = (size_t)width * height;
    unsigned changed = 0;
    if (!texture_layout(map, map_size, target->texture, width, height, &palette,
                        &pixels))
      continue;
    for (flipped = 0; flipped <= 1; ++flipped) {
      for (swapped = 0; swapped <= 1; ++swapped) {
        if (!matches(target, palette, pixels, surface, pitch, width, height,
                     flipped, swapped))
          continue;
        for (pos = 0; pos < count; ++pos) {
          unsigned x = (unsigned)(pos % width);
          unsigned y = (unsigned)(pos / width);
          uint8_t *dst;
          if (!selected(target, pixels[pos]))
            continue;
          dst = surface + (flipped ? height - 1u - y : y) * pitch + 2u * x;
          if ((le16(dst) & 0x7fff) == 0) {
            put16(dst, (uint16_t)((le16(dst) & 0x8000) | 0x0421));
            ++changed;
          }
        }
        return changed;
      }
    }
  }
  return 0;
}
