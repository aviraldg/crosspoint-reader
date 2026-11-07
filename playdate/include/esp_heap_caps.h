#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#define MALLOC_CAP_DEFAULT (1U << 0)
#define MALLOC_CAP_8BIT (1U << 2)
#define MALLOC_CAP_INTERNAL (1U << 11)
#define MALLOC_CAP_SPIRAM (1U << 10)

struct multi_heap_info_t {
  size_t total_free_bytes;
  size_t total_allocated_bytes;
  size_t largest_free_block;
  size_t minimum_free_bytes;
  size_t allocated_blocks;
  size_t free_blocks;
  size_t total_blocks;
};

inline void* heap_caps_malloc(size_t size, uint32_t caps) {
  if ((caps & MALLOC_CAP_SPIRAM) != 0 && (caps & MALLOC_CAP_DEFAULT) == 0) {
    return nullptr;
  }
  return std::malloc(size);
}

inline void* heap_caps_calloc(size_t n, size_t size, uint32_t caps) {
  if ((caps & MALLOC_CAP_SPIRAM) != 0 && (caps & MALLOC_CAP_DEFAULT) == 0) {
    return nullptr;
  }
  return std::calloc(n, size);
}

inline void* heap_caps_realloc(void* ptr, size_t size, uint32_t caps) {
  if ((caps & MALLOC_CAP_SPIRAM) != 0 && (caps & MALLOC_CAP_DEFAULT) == 0) {
    return nullptr;
  }
  return std::realloc(ptr, size);
}

inline void heap_caps_free(void* ptr) { std::free(ptr); }

inline size_t heap_caps_get_free_size(uint32_t caps) {
  if ((caps & MALLOC_CAP_SPIRAM) != 0) return 0;
  return 12 * 1024 * 1024;
}

inline size_t heap_caps_get_largest_free_block(uint32_t caps) {
  if ((caps & MALLOC_CAP_SPIRAM) != 0) return 0;
  return 8 * 1024 * 1024;
}

inline size_t heap_caps_get_minimum_free_size(uint32_t caps) {
  if ((caps & MALLOC_CAP_SPIRAM) != 0) return 0;
  return 8 * 1024 * 1024;
}

inline size_t heap_caps_get_total_size(uint32_t caps) {
  if ((caps & MALLOC_CAP_SPIRAM) != 0) return 0;
  return 16 * 1024 * 1024;
}

inline void heap_caps_get_info(multi_heap_info_t* info, uint32_t caps) {
  if (!info) return;
  if ((caps & MALLOC_CAP_SPIRAM) != 0) {
    *info = {};
    return;
  }
  info->total_free_bytes = 12 * 1024 * 1024;
  info->total_allocated_bytes = 4 * 1024 * 1024;
  info->largest_free_block = 8 * 1024 * 1024;
  info->minimum_free_bytes = 8 * 1024 * 1024;
  info->allocated_blocks = 128;
  info->free_blocks = 16;
  info->total_blocks = 144;
}
