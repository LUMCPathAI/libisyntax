#pragma once

#include "isyntax.h"
#include "libisyntax.h"
#include "platform_mutex.h"

typedef struct isyntax_tile_list_t {
    isyntax_tile_t* head;
    isyntax_tile_t* tail;
    int count;
    const char* dbg_name;
} isyntax_tile_list_t;

typedef struct isyntax_cache_t {
    isyntax_tile_list_t cache_list;
    platform_mutex_t mutex;
    platform_cond_t tile_ready; // Signaled when a read finishes decoding or transforming tiles.
    // TODO(avirodov): int refcount;
    int target_cache_size;
    // When not 0, tiles are also evicted until their coefficients fit in this many bytes.
    int64_t capacity_bytes;
    block_allocator_t* ll_coeff_block_allocator;
    block_allocator_t* h_coeff_block_allocator;
	bool is_block_allocator_owned;
    int allocator_block_width;
    int allocator_block_height;
} isyntax_cache_t;

// Bytes of the coefficients the cache holds, tiles in use by reads included.
int64_t isyntax_cache_used_bytes(isyntax_cache_t* cache);
// Takes the tile out of the cache list and frees its coefficients. Called with the cache mutex held.
void isyntax_cache_drop_tile(isyntax_cache_t* cache, isyntax_tile_t* tile);

// TODO(avirodov): can this ever fail?
void isyntax_tile_read(isyntax_t* isyntax, isyntax_cache_t* cache, int scale, int tile_x, int tile_y,
                       uint32_t* pixels_buffer, enum isyntax_pixel_format_t pixel_format);

void tile_list_init(isyntax_tile_list_t* list, const char* dbg_name);
void tile_list_remove(isyntax_tile_list_t* list, isyntax_tile_t* tile);
