/*
  BSD 2-Clause License

  Copyright (c) 2019-2025, Pieter Valkema, Alexandr Virodov

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this
     list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "common.h"
#include "isyntax_reader.h"

#define LOG(msg, ...) console_print(msg, ##__VA_ARGS__)
#define LOG_VAR(fmt, var) console_print("%s: %s=" fmt "\n", __FUNCTION__, #var, var)

void tile_list_init(isyntax_tile_list_t* list, const char* dbg_name) {
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
    list->dbg_name = dbg_name;
}

void tile_list_remove(isyntax_tile_list_t* list, isyntax_tile_t* tile) {
    if (!tile->cache_next && !tile->cache_prev && !(list->head == tile) && !(list->tail == tile)) {
        // Not part of any list.
        return;
    }
    if (list->head == tile) {
        list->head = tile->cache_next;
    }
    if (list->tail == tile) {
        list->tail = tile->cache_prev;
    }
    if (tile->cache_prev) {
        tile->cache_prev->cache_next = tile->cache_next;
    }
    if (tile->cache_next) {
        tile->cache_next->cache_prev = tile->cache_prev;
    }
    // Here we assume that the tile is part of this list, but we don't check (O(n)).
    tile->cache_next = NULL;
    tile->cache_prev = NULL;
    list->count--;
}

static void tile_list_insert_first(isyntax_tile_list_t* list, isyntax_tile_t* tile) {
    // printf("### tile_list_insert_first %s scale=%d x=%d y=%d\n", list->dbg_name, tile->tile_scale, tile->tile_x, tile->tile_y);
    ASSERT(tile->cache_next == NULL && tile->cache_prev == NULL);
    if (list->head == NULL) {
        list->head = tile;
        list->tail = tile;
    } else {
        list->head->cache_prev = tile;
        tile->cache_next = list->head;
        list->head = tile;
    }
    list->count++;
}

// Tiles a read needs. Only the read that collected them uses this list, unlike the lists linked through the tiles.
typedef struct tile_array_t {
    isyntax_tile_t** tiles;
    int count;
    int capacity;
} tile_array_t;

static void tile_array_push(tile_array_t* array, isyntax_tile_t* tile) {
    if (array->count == array->capacity) {
        array->capacity = array->capacity ? 2 * array->capacity : 32;
        array->tiles = realloc(array->tiles, array->capacity * sizeof(isyntax_tile_t*));
    }
    array->tiles[array->count++] = tile;
}

// Codeblocks a read decodes for one tile, out of the cache lock.
typedef struct coeff_load_t {
    isyntax_tile_t* tile;
    bool ll;
    bool h;
    icoeff_t* coeff_ll[3];
    icoeff_t* coeff_h[3];
} coeff_load_t;

static bool tile_needs_ll_codeblocks(isyntax_image_t* wsi, isyntax_tile_t* tile) {
    // Only top-level tiles read their LL coefficients from the file. For other levels, the LL coefficients are
    // computed from parent tiles.
    return tile->exists && tile->tile_scale == wsi->max_scale && !tile->has_ll;
}

static bool tile_needs_h_codeblocks(isyntax_tile_t* tile) {
    return tile->exists && !tile->has_h;
}

static void isyntax_openslide_decode_codeblocks(isyntax_cache_t* cache, isyntax_t* isyntax, isyntax_tile_t* tile,
                                                int codeblock_index, bool is_ll, icoeff_t* out_coeff[3]) {
    isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
    isyntax_data_chunk_t* chunk = &wsi->data_chunks[tile->data_chunk_index];

    for (int color = 0; color < 3; ++color) {
        isyntax_codeblock_t* codeblock = &wsi->codeblocks[codeblock_index + color * chunk->codeblock_count_per_color];
        ASSERT(codeblock->coefficient == (is_ll ? 0 : 1)); // LL coefficient codeblock for this tile.
        // TODO(avirodov): int vs i32 vs u32 consistently.
        ASSERT(codeblock->color_component == (u32)color);
        ASSERT(codeblock->scale == (u32)tile->tile_scale);
        out_coeff[color] = (icoeff_t *) block_alloc(is_ll ? cache->ll_coeff_block_allocator : cache->h_coeff_block_allocator);
        // TODO(avirodov): fancy allocators, for multiple sequential blocks (aka chunk). Or let OS do the caching.
        // Adding 7 safety bytes so bitstream_lsb_read() won't access out of bounds in isyntax_hulsken_decompress().
        u8* codeblock_data = malloc(codeblock->block_size + 7);
        size_t bytes_read = file_handle_read_at_offset(codeblock_data, isyntax->file_handle,
                                                       codeblock->block_data_offset, codeblock->block_size);
        if (!(bytes_read > 0)) {
            console_print_error("Error: could not read iSyntax data at offset %lld (read size %lld)\n",
                                codeblock->block_data_offset, codeblock->block_size);
        }

        isyntax_hulsken_decompress(codeblock_data, codeblock->block_size,
                                   isyntax->block_width, isyntax->block_height,
                                   codeblock->coefficient, wsi->compressor_version, out_coeff[color]);
        free(codeblock_data);
    }
}

static void isyntax_openslide_decode_tile_coefficients(isyntax_cache_t* cache, isyntax_t* isyntax, coeff_load_t* load) {
    isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
    isyntax_tile_t* tile = load->tile;

    if (load->ll) {
        isyntax_openslide_decode_codeblocks(cache, isyntax, tile, /*codeblock_index=*/tile->codeblock_index,
                                            /*is_ll=*/true, load->coeff_ll);
    }

    if (load->h) {
        isyntax_data_chunk_t* chunk = wsi->data_chunks + tile->data_chunk_index;

        i32 scale_in_chunk = chunk->scale - tile->tile_scale;
        ASSERT(scale_in_chunk >= 0 && scale_in_chunk < 3);
        i32 codeblock_index_in_chunk = 0;
        if (scale_in_chunk == 0) {
            codeblock_index_in_chunk = 0;
        } else if (scale_in_chunk == 1) {
            codeblock_index_in_chunk = 1 + (tile->tile_y % 2) * 2 + (tile->tile_x % 2);
        } else if (scale_in_chunk == 2) {
            codeblock_index_in_chunk = 5 + (tile->tile_y % 4) * 4 + (tile->tile_x % 4);
        } else {
            fatal_error();
        }

        isyntax_openslide_decode_codeblocks(cache, isyntax, tile,
                                            /*codeblock_index=*/tile->codeblock_chunk_index + codeblock_index_in_chunk,
                                            /*is_ll=*/false, load->coeff_h);
    }
}

// Decodes the codeblocks the tiles miss. Called with the cache lock held, which it releases while decoding. Reads
// claim one tile at a time, so that reads needing the same tiles decode them in parallel; tiles another read is
// decoding are waited for.
static void isyntax_openslide_load_tile_coefficients(isyntax_cache_t* cache, isyntax_t* isyntax,
                                                     const tile_array_t* tiles) {
    isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
    for (;;) {
        coeff_load_t load = {0};
        bool is_waiting = false;
        for (int i = 0; i < tiles->count && !load.tile; ++i) {
            isyntax_tile_t* tile = tiles->tiles[i];
            bool ll = tile_needs_ll_codeblocks(wsi, tile);
            bool h = tile_needs_h_codeblocks(tile);
            if (!ll && !h) {
                continue;
            }
            if (tile->is_coeff_loading) {
                is_waiting = true;
                continue;
            }
            tile->is_coeff_loading = true;
            load = (coeff_load_t){.tile = tile, .ll = ll, .h = h};
        }

        if (load.tile) {
            platform_mutex_unlock(&cache->mutex);
            isyntax_openslide_decode_tile_coefficients(cache, isyntax, &load);
            platform_mutex_lock(&cache->mutex);
            for (int color = 0; color < 3; ++color) {
                if (load.ll) {
                    load.tile->color_channels[color].coeff_ll = load.coeff_ll[color];
                }
                if (load.h) {
                    load.tile->color_channels[color].coeff_h = load.coeff_h[color];
                }
            }
            if (load.ll) {
                load.tile->has_ll = true;
            }
            if (load.h) {
                load.tile->has_h = true;
            }
            load.tile->is_coeff_loading = false;
            platform_cond_broadcast(&cache->tile_ready);
        } else if (is_waiting) {
            platform_cond_wait(&cache->tile_ready, &cache->mutex);
        } else {
            break;
        }
    }
}

typedef union isyntax_tile_children_t {
    struct {
        isyntax_tile_t *child_top_left;
        isyntax_tile_t *child_top_right;
        isyntax_tile_t *child_bottom_left;
        isyntax_tile_t *child_bottom_right;
    };
    isyntax_tile_t* as_array[4];
} isyntax_tile_children_t;

static isyntax_tile_children_t isyntax_openslide_compute_children(isyntax_t* isyntax, isyntax_tile_t* tile) {
    isyntax_tile_children_t result;
    isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
    ASSERT(tile->tile_scale > 0);
    isyntax_level_t *next_level = &wsi->levels[tile->tile_scale - 1];
    result.child_top_left = next_level->tiles + (tile->tile_y * 2) * next_level->width_in_tiles + (tile->tile_x * 2);
    result.child_top_right = result.child_top_left + 1;
    result.child_bottom_left = result.child_top_left + next_level->width_in_tiles;
    result.child_bottom_right = result.child_bottom_left + 1;
    return result;
}

static bool isyntax_openslide_children_have_ll(isyntax_t* isyntax, isyntax_tile_t* tile) {
    if (tile->tile_scale == 0) {
        return true;
    }
    isyntax_tile_children_t children = isyntax_openslide_compute_children(isyntax, tile);
    return children.child_top_left->has_ll && children.child_top_right->has_ll &&
           children.child_bottom_left->has_ll && children.child_bottom_right->has_ll;
}

// Inverse wavelet transform of a tile: writes the LL coefficients its children miss, and its pixels if asked.
// Called with the cache lock held, which it releases while transforming. Only one read at a time writes the LL
// coefficients of the children of a tile; the others wait for it.
static void isyntax_openslide_idwt(isyntax_cache_t* cache, isyntax_t* isyntax, isyntax_tile_t* tile,
                                   uint32_t* pixels_buffer, enum isyntax_pixel_format_t pixel_format) {
    while (tile->is_idwt_running) {
        platform_cond_wait(&cache->tile_ready, &cache->mutex);
    }

    // If all children have ll coefficients and we don't need the rgb pixels, no need to do the idwt.
    bool writes_children = !isyntax_openslide_children_have_ll(isyntax, tile);
    if (!writes_children && pixels_buffer == NULL) {
        return;
    }

    // TODO(avirodov): if we want rgb from tile where idwt was done already, this could be cheaper if we store
    //  the lls in the tile. Currently need to recompute idwt.
    tile->is_idwt_running = writes_children;
    platform_mutex_unlock(&cache->mutex);
    isyntax_load_tile_concurrent(isyntax, &isyntax->images[isyntax->wsi_image_index],
                                 tile->tile_scale, tile->tile_x, tile->tile_y,
                                 cache->ll_coeff_block_allocator,
                                 pixels_buffer, pixel_format);
    platform_mutex_lock(&cache->mutex);
    if (writes_children) {
        isyntax_tile_children_t children = isyntax_openslide_compute_children(isyntax, tile);
        for (int i = 0; i < 4; ++i) {
            children.as_array[i]->has_ll = true;
        }
        tile->is_idwt_running = false;
        platform_cond_broadcast(&cache->tile_ready);
    }
}

static void isyntax_make_tile_lists_add_parent_to_list(isyntax_t* isyntax, isyntax_tile_t* tile,
                                                       tile_array_t* idwt_list) {
    isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
    int parent_tile_scale = tile->tile_scale + 1;
    if (parent_tile_scale > wsi->max_scale) {
        return;
    }

    int parent_tile_x = tile->tile_x / 2;
    int parent_tile_y = tile->tile_y / 2;
    isyntax_level_t* parent_level = &wsi->levels[parent_tile_scale];
    isyntax_tile_t* parent_tile = &parent_level->tiles[parent_level->width_in_tiles * parent_tile_y + parent_tile_x];
    if (parent_tile->exists && !parent_tile->cache_marked) {
        parent_tile->cache_marked = true;
        tile_array_push(idwt_list, parent_tile);
    }
}

static void isyntax_make_tile_lists_add_children_to_list(isyntax_t* isyntax, isyntax_tile_t* tile,
                                                         tile_array_t* children_list) {
    if (tile->tile_scale > 0) {
        isyntax_tile_children_t children = isyntax_openslide_compute_children(isyntax, tile);
        for (int i = 0; i < 4; ++i) {
            if (!children.as_array[i]->cache_marked) {
                children.as_array[i]->cache_marked = true;
                tile_array_push(children_list, children.as_array[i]);
            }
        }
    }
}

// Lists are ordered children first: parents are at the end.
static void isyntax_make_tile_lists_by_scale(isyntax_t* isyntax, int start_scale,
                                             tile_array_t* idwt_list,
                                             tile_array_t* coeff_list,
                                             tile_array_t* children_list) {
    isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
    for (int scale = start_scale; scale <= wsi->max_scale; ++scale) {
        // Mark all neighbors of idwt tiles at this level as requiring coefficients.
        isyntax_level_t* level = &wsi->levels[scale];
        for (int i = 0; i < idwt_list->count; ++i) {
            isyntax_tile_t* tile = idwt_list->tiles[i];
            if (tile->tile_scale == scale) {
                for (int y_offset = -1; y_offset <= 1; ++y_offset) {
                    for (int x_offset = -1; x_offset <= 1; ++ x_offset) {
                        int neighbor_tile_x =  tile->tile_x + x_offset;
                        int neighbor_tile_y = tile->tile_y + y_offset;
                        if (neighbor_tile_x < 0 || neighbor_tile_x >= level->width_in_tiles ||
                            neighbor_tile_y < 0 || neighbor_tile_y >= level->height_in_tiles) {
                            continue;
                        }

                        isyntax_tile_t* neighbor_tile = &level->tiles[level->width_in_tiles * neighbor_tile_y + neighbor_tile_x];
                        if (neighbor_tile->cache_marked || !neighbor_tile->exists) {
                            continue;
                        }

                        neighbor_tile->cache_marked = true;
                        tile_array_push(coeff_list, neighbor_tile);
                    }
                }
            }
        }

        // Mark all parents of tiles at this level as requiring idwt. This way all tiles at this level will get their
        // ll coefficients.
        for (int i = 0; i < idwt_list->count; ++i) {
            if (idwt_list->tiles[i]->tile_scale == scale) {
                isyntax_make_tile_lists_add_parent_to_list(isyntax, idwt_list->tiles[i], idwt_list);
            }
        }
        for (int i = 0; i < coeff_list->count; ++i) {
            if (coeff_list->tiles[i]->tile_scale == scale) {
                isyntax_make_tile_lists_add_parent_to_list(isyntax, coeff_list->tiles[i], idwt_list);
            }
        }
    }

    // Add all children of idwt that were not yet handled. The children will have their ll coefficients written,
    // and so should be cache bumped.
    // TODO(avirodov): if we store the idwt result (ll of next level) in the tile instead of the children, this
    //  would be unnecessary. But I'm not sure this is bad either.
    for (int i = 0; i < idwt_list->count; ++i) {
        isyntax_make_tile_lists_add_children_to_list(isyntax, idwt_list->tiles[i], children_list);
    }
}

// Takes the tiles out of the cache list while a read uses them, so that other reads cannot evict them.
static void tile_array_pin(isyntax_cache_t* cache, const tile_array_t* array) {
    for (int i = 0; i < array->count; ++i) {
        isyntax_tile_t* tile = array->tiles[i];
        tile->cache_marked = false;
        if (tile->cache_pin_count++ == 0) {
            tile_list_remove(&cache->cache_list, tile);
        }
    }
}

// Puts the tiles no read uses anymore back first in the cache list, the last ones of the array (parents) at the head.
static void tile_array_unpin(isyntax_cache_t* cache, const tile_array_t* array) {
    for (int i = 0; i < array->count; ++i) {
        isyntax_tile_t* tile = array->tiles[i];
        if (--tile->cache_pin_count == 0) {
            tile_list_insert_first(&cache->cache_list, tile);
        }
    }
}

int64_t isyntax_cache_used_bytes(isyntax_cache_t* cache) {
    return (int64_t)(block_allocator_used_bytes(cache->ll_coeff_block_allocator) +
                     block_allocator_used_bytes(cache->h_coeff_block_allocator));
}

void isyntax_cache_drop_tile(isyntax_cache_t* cache, isyntax_tile_t* tile) {
    tile_list_remove(&cache->cache_list, tile);
    for (int i = 0; i < 3; ++i) {
        if (tile->color_channels[i].coeff_ll) {
            block_free(cache->ll_coeff_block_allocator, tile->color_channels[i].coeff_ll);
            tile->color_channels[i].coeff_ll = NULL;
        }
        if (tile->color_channels[i].coeff_h) {
            block_free(cache->h_coeff_block_allocator, tile->color_channels[i].coeff_h);
            tile->color_channels[i].coeff_h = NULL;
        }
    }
    tile->has_ll = false;
    tile->has_h = false;
}

void isyntax_tile_read(isyntax_t* isyntax, isyntax_cache_t* cache, int scale, int tile_x, int tile_y,
                       uint32_t* pixels_buffer, enum isyntax_pixel_format_t pixel_format) {
    // The cache lock guards the tiles' cache fields and the lists, and is released while decoding and transforming:
    // reads run in parallel. A read pins the tiles it needs so that they are not evicted, and claims the work it does
    // on a tile (is_coeff_loading, is_idwt_running) so that other reads needing it wait instead of redoing it.

    // Decoding uses memory local to the thread.
    if (!threadlocal_thread_memory) {
        init_thread_memory(&global_system_info);
    }

    isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
    isyntax_level_t* level = &wsi->levels[scale];

	if (!(tile_x >= 0 && tile_x < level->width_in_tiles && tile_y >= 0 && tile_y < level->height_in_tiles)) {
		// Read out of bounds -> set to all white
		memset(pixels_buffer, 0xff, isyntax->tile_width * isyntax->tile_height * 4);
		return;
	}

    isyntax_tile_t *tile = &level->tiles[level->width_in_tiles * tile_y + tile_x];
    // printf("=== isyntax_openslide_load_tile scale=%d tile_x=%d tile_y=%d\n", scale, tile_x, tile_y);
    if (!tile->exists) {
        memset(pixels_buffer, 0xff, isyntax->tile_width * isyntax->tile_height * 4);
        return;
    }

    // Need 3 lists:
    // 1. idwt list - those tiles will have to perform an idwt for their children to get ll coeffs. Primary cache bump.
    // 2. coeff list - those tiles are neighbors and will need to have coefficients loaded. Secondary cache bump.
    // 3. children list - those tiles will have their ll coeffs loaded as a side effect. Tertiary cache bump.
    // Those lists are disjoint.
    tile_array_t idwt_list = {0};
    tile_array_t coeff_list = {0};
    tile_array_t children_list = {0};

    platform_mutex_lock(&cache->mutex);

    tile->cache_marked = true;
    tile_array_push(&idwt_list, tile);
    isyntax_make_tile_lists_by_scale(isyntax, scale, &idwt_list, &coeff_list, &children_list);
    tile_array_pin(cache, &idwt_list);
    tile_array_pin(cache, &coeff_list);
    tile_array_pin(cache, &children_list);

    // IO+decode: For all dependent tiles, read and decode coefficients where missing (hh, and ll for top tiles).
    tile_array_t coeff_tiles = {0};
    for (int i = 0; i < coeff_list.count; ++i) tile_array_push(&coeff_tiles, coeff_list.tiles[i]);
    for (int i = 0; i < idwt_list.count; ++i) tile_array_push(&coeff_tiles, idwt_list.tiles[i]);
    isyntax_openslide_load_tile_coefficients(cache, isyntax, &coeff_tiles);
    free(coeff_tiles.tiles);

    // IDWT as needed, top to bottom: parents give their children the LL coefficients the neighbors at the next
    // scale need. The requested tile, first in the list, comes last and produces the pixels.
    // YCoCb->RGB for this tile only.
    for (int s = wsi->max_scale; s > scale; --s) {
        for (int i = 1; i < idwt_list.count; ++i) {
            if (idwt_list.tiles[i]->tile_scale == s) {
                isyntax_openslide_idwt(cache, isyntax, idwt_list.tiles[i], /*pixels_buffer=*/NULL, /*pixel_format=*/0);
            }
        }
    }
    isyntax_openslide_idwt(cache, isyntax, tile, pixels_buffer, pixel_format);

    // Bump all the affected tiles in cache.
    tile_array_unpin(cache, &children_list);
    tile_array_unpin(cache, &coeff_list);
    tile_array_unpin(cache, &idwt_list);

    // Cache trim. Since we have the result already, it is possible that tiles from this run will be trimmed here
    // if cache is small or work happened on other threads. Tiles other reads use are not in the list.
    while (cache->cache_list.tail &&
           (cache->cache_list.count > cache->target_cache_size ||
            (cache->capacity_bytes > 0 && isyntax_cache_used_bytes(cache) > cache->capacity_bytes))) {
        isyntax_cache_drop_tile(cache, cache->cache_list.tail);
    }

    // Prevent iSyntax streamer from calling isyntax_begin_first_load()
    if (!wsi->first_load_complete) {
        wsi->first_load_complete = true;
    }

    platform_mutex_unlock(&cache->mutex);

    free(idwt_list.tiles);
    free(coeff_list.tiles);
    free(children_list.tiles);
}
