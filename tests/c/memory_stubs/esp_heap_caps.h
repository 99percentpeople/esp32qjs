#pragma once
#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_INTERNAL 1U
#define MALLOC_CAP_SPIRAM 2U
#define MALLOC_CAP_8BIT 4U
#define MALLOC_CAP_DMA 8U
typedef struct { size_t total_free_bytes, largest_free_block; } multi_heap_info_t;
void heap_caps_get_info(multi_heap_info_t *info, uint32_t caps);
size_t heap_caps_get_total_size(uint32_t caps);
void *heap_caps_malloc(size_t size, uint32_t caps);
void *heap_caps_calloc(size_t count, size_t size, uint32_t caps);
void *heap_caps_realloc(void *data, size_t size, uint32_t caps);
void heap_caps_free(void *data);
