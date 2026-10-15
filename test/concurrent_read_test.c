// Tiles read by many threads sharing one cache have the pixels of tiles read by a single thread. Two isyntax objects
// of the slide share the cache, which is small, so that the threads evict the tiles of each other while reading.
// Flushing the tiles of one isyntax frees its coefficients only.
#include "common.h"

#if WINDOWS
int main(void) {
    printf("Test disabled on windows.");
    return 0;
}
#else

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "libisyntax.h"

#define THREAD_COUNT 8
#define WINDOW_TILES 8 // Tiles read per level: a window of WINDOW_TILES x WINDOW_TILES around the center.
#define SHARED_CACHE_SIZE 64
#define SHARED_CACHE_BYTES (32 << 20)

typedef struct tile_ref_t {
    int32_t level;
    int32_t x;
    int32_t y;
    uint64_t hash;
} tile_ref_t;

typedef struct reader_t {
    isyntax_t* isyntax;
    isyntax_cache_t* cache;
    tile_ref_t* tiles;
    int tile_count;
    uint32_t seed;
    int mismatches;
} reader_t;

static uint64_t hash_pixels(const uint32_t* pixels, size_t count) {
    uint64_t hash = 1469598103934665603ull; // FNV-1a
    const uint8_t* bytes = (const uint8_t*)pixels;
    for (size_t i = 0; i < count * 4; ++i) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
    return hash;
}

static uint64_t read_tile_hash(isyntax_t* isyntax, isyntax_cache_t* cache, const tile_ref_t* tile, uint32_t* pixels) {
    size_t count = (size_t)libisyntax_get_tile_width(isyntax) * libisyntax_get_tile_height(isyntax);
    if (libisyntax_tile_read(isyntax, cache, tile->level, tile->x, tile->y, pixels, LIBISYNTAX_PIXEL_FORMAT_RGBA)
            != LIBISYNTAX_OK) {
        return 0;
    }
    return hash_pixels(pixels, count);
}

static void* read_all_shuffled(void* arg) {
    reader_t* reader = (reader_t*)arg;
    int* order = malloc(reader->tile_count * sizeof(int));
    for (int i = 0; i < reader->tile_count; ++i) order[i] = i;
    uint32_t state = reader->seed;
    for (int i = reader->tile_count - 1; i > 0; --i) {
        state = state * 1664525u + 1013904223u;
        int j = (int)(state % (uint32_t)(i + 1));
        int t = order[i]; order[i] = order[j]; order[j] = t;
    }
    uint32_t* pixels = malloc(sizeof(uint32_t) * libisyntax_get_tile_width(reader->isyntax) *
                              libisyntax_get_tile_height(reader->isyntax));
    for (int i = 0; i < reader->tile_count; ++i) {
        const tile_ref_t* tile = &reader->tiles[order[i]];
        if (read_tile_hash(reader->isyntax, reader->cache, tile, pixels) != tile->hash) {
            if (reader->mismatches++ < 5) {
                fprintf(stderr, "seed %u: tile level=%d x=%d y=%d differs\n", reader->seed, tile->level, tile->x,
                        tile->y);
            }
        }
    }
    free(pixels);
    free(order);
    return NULL;
}

static isyntax_t* open_with_cache(const char* path, isyntax_cache_t* cache) {
    isyntax_t* isyntax = NULL;
    if (libisyntax_open(path, 0, &isyntax) != LIBISYNTAX_OK) return NULL;
    if (libisyntax_cache_inject(cache, isyntax) != LIBISYNTAX_OK) return NULL;
    return isyntax;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s testslide.isyntax\n", argv[0]);
        return 1;
    }
    if (libisyntax_init() != LIBISYNTAX_OK) return 1;

    // Reference pixels, read by one thread with a cache holding every tile.
    isyntax_cache_t* reference_cache = NULL;
    if (libisyntax_cache_create("reference", 1 << 20, &reference_cache) != LIBISYNTAX_OK) return 1;
    isyntax_t* reference = open_with_cache(argv[1], reference_cache);
    if (!reference) return 1;
    const isyntax_image_t* wsi = libisyntax_get_wsi_image(reference);
    int32_t level_count = libisyntax_image_get_level_count(wsi);
    tile_ref_t* tiles = malloc(sizeof(tile_ref_t) * level_count * WINDOW_TILES * WINDOW_TILES);
    int tile_count = 0;
    for (int32_t level = 0; level < level_count; ++level) {
        const isyntax_level_t* l = libisyntax_image_get_level(wsi, level);
        int32_t width = libisyntax_level_get_width_in_tiles(l);
        int32_t height = libisyntax_level_get_height_in_tiles(l);
        int32_t x0 = MAX(0, width / 2 - WINDOW_TILES / 2);
        int32_t y0 = MAX(0, height / 2 - WINDOW_TILES / 2);
        for (int32_t y = y0; y < MIN(height, y0 + WINDOW_TILES); ++y) {
            for (int32_t x = x0; x < MIN(width, x0 + WINDOW_TILES); ++x) {
                tiles[tile_count++] = (tile_ref_t){.level = level, .x = x, .y = y};
            }
        }
    }
    uint32_t* pixels = malloc(sizeof(uint32_t) * libisyntax_get_tile_width(reference) *
                              libisyntax_get_tile_height(reference));
    for (int i = 0; i < tile_count; ++i) {
        tiles[i].hash = read_tile_hash(reference, reference_cache, &tiles[i], pixels);
    }
    free(pixels);

    // The same tiles, read in a different order by every thread, through one small cache shared by two isyntax.
    isyntax_cache_t* shared_cache = NULL;
    if (libisyntax_cache_create("shared", 1 << 20, &shared_cache) != LIBISYNTAX_OK) return 1;
    if (libisyntax_cache_set_capacity_bytes(shared_cache, SHARED_CACHE_BYTES) != LIBISYNTAX_OK) return 1;
    isyntax_t* shared[2] = {open_with_cache(argv[1], shared_cache), open_with_cache(argv[1], shared_cache)};
    if (!shared[0] || !shared[1]) return 1;
    reader_t readers[THREAD_COUNT];
    pthread_t threads[THREAD_COUNT];
    for (int i = 0; i < THREAD_COUNT; ++i) {
        readers[i] = (reader_t){.isyntax = shared[i % 2], .cache = shared_cache, .tiles = tiles,
                                .tile_count = tile_count, .seed = 1234u + i};
        if (pthread_create(&threads[i], NULL, read_all_shuffled, &readers[i]) != 0) return 1;
    }
    int mismatches = 0;
    for (int i = 0; i < THREAD_COUNT; ++i) {
        pthread_join(threads[i], NULL);
        mismatches += readers[i].mismatches;
    }
    printf("%d threads read %d tiles each, %d differ from a single thread\n", THREAD_COUNT, tile_count, mismatches);
    int64_t used = libisyntax_cache_get_used_bytes(shared_cache);
    if (used > SHARED_CACHE_BYTES) {
        fprintf(stderr, "the cache holds %lld bytes, over its capacity\n", (long long)used);
        ++mismatches;
    }

    // Flushing one isyntax frees its coefficients, not those of the other.
    libisyntax_cache_flush(shared_cache, NULL);
    uint32_t* top_pixels = malloc(sizeof(uint32_t) * libisyntax_get_tile_width(reference) *
                                  libisyntax_get_tile_height(reference));
    const tile_ref_t* top = NULL; // The last tile read that holds data.
    for (int i = tile_count - 1; i >= 0 && libisyntax_cache_get_used_bytes(shared_cache) == 0; --i) {
        top = &tiles[i];
        read_tile_hash(shared[0], shared_cache, top, top_pixels);
    }
    int64_t used_by_first = libisyntax_cache_get_used_bytes(shared_cache);
    read_tile_hash(shared[1], shared_cache, top, top_pixels);
    libisyntax_cache_flush(shared_cache, shared[1]);
    if (used_by_first == 0 || libisyntax_cache_get_used_bytes(shared_cache) != used_by_first) {
        fprintf(stderr, "flushing the second isyntax left %lld bytes, the first one used %lld\n",
                (long long)libisyntax_cache_get_used_bytes(shared_cache), (long long)used_by_first);
        ++mismatches;
    }
    free(top_pixels);

    libisyntax_close(shared[1]);
    libisyntax_cache_flush(shared_cache, shared[0]);
    libisyntax_close(shared[0]);
    libisyntax_cache_destroy(shared_cache);
    libisyntax_close(reference);
    libisyntax_cache_destroy(reference_cache);
    free(tiles);
    return mismatches == 0 ? 0 : 1;
}

#endif
