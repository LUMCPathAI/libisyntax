/*
  BSD 2-Clause License

  Copyright (c) 2019-2026, Pieter Valkema

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

// To compile without implementing stb_printf.h:
// #define LIBISYNTAX_NO_STB_SPRINTF_IMPLEMENTATION

// To compile without implementing stb_image.h.:
// #define LIBISYNTAX_NO_STB_IMAGE_IMPLEMENTATION

// To compile without implementing thread pool routines (in case you want to supply your own):
// #define LIBISYNTAX_NO_THREAD_POOL_IMPLEMENTATION

#if __has_include("config.h")
#include "config.h"
#endif

#ifndef LIBISYNTAX_NO_STB_SPRINTF_IMPLEMENTATION
#define STB_SPRINTF_IMPLEMENTATION
#endif
#include "stb_sprintf.h"

#ifndef LIBISYNTAX_NO_STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#endif
#include "stb_image.h"

#include "common.h"
#include "platform.h"
#include "intrinsics.h"

#include "libisyntax.h"
#include "isyntax.h"
#include "isyntax_reader.h"
#include <math.h>

#define CHECK_LIBISYNTAX_OK(_libisyntax_call) do { \
    isyntax_error_t result = _libisyntax_call;     \
    ASSERT(result == LIBISYNTAX_OK);               \
} while(0);

// Routines for initializing the global thread pool

// TODO(avirodov): int may be too small for some counters later on.
// TODO(avirodov): should make a flag to turn counters off, they may have overhead.
// TODO(avirodov): struct? move to isyntax.h/.c?
// TODO(avirodov): debug api?
#define DBGCTR_COUNT(_counter) atomic_increment(&_counter)
i32 volatile dbgctr_init_thread_pool_counter = 0;

static platform_mutex_t libisyntax_global_mutex = PLATFORM_MUTEX_INITIALIZER;

isyntax_error_t libisyntax_init() {
    // Lock-unlock to ensure that all parallel calls to libisyntax_init() wait for the actual initialization to complete.
    platform_mutex_lock(&libisyntax_global_mutex);
    static bool libisyntax_global_init_complete = false;

    if (libisyntax_global_init_complete == false) {
        // Actual initialization.
#ifndef LIBISYNTAX_THREAD_POOL_SHARED_WITH_SLIDESCAPE
        init_global_system_info(false);
        DBGCTR_COUNT(dbgctr_init_thread_pool_counter);
        init_thread_pool(&global_thread_pool,
                         1024,
                         false,
                         false,
                         NULL);

#else
        libisyntax_init_thread_pool_for_slidescape();
#endif
        libisyntax_global_init_complete = true;
    }
    platform_mutex_unlock(&libisyntax_global_mutex);
    return LIBISYNTAX_OK;
}

isyntax_error_t libisyntax_open(const char* filename, enum libisyntax_open_flags_t flags, isyntax_t** out_isyntax) {
    // Note(avirodov): intentionally not changing api of isyntax_open. We can do that later if needed and reduce
    // the size/count of wrappers.
    isyntax_t* result = malloc(sizeof(isyntax_t));
    memset(result, 0, sizeof(*result));

    bool success = isyntax_open(result, filename, flags);
    if (success) {
        *out_isyntax = result;
        return LIBISYNTAX_OK;
    } else {
        free(result);
        return LIBISYNTAX_FATAL;
    }
}

void libisyntax_close(isyntax_t* isyntax) {
    isyntax_destroy(isyntax);
    free(isyntax);
}

int32_t libisyntax_get_tile_width(const isyntax_t* isyntax) {
    return isyntax->tile_width;
}

int32_t libisyntax_get_tile_height(const isyntax_t* isyntax) {
    return isyntax->tile_height;
}

const isyntax_image_t* libisyntax_get_wsi_image(const isyntax_t* isyntax) {
    return isyntax->images + isyntax->wsi_image_index;
}

int32_t libisyntax_get_is_mpp_known(const isyntax_t* isyntax) {
    return isyntax->is_mpp_known;
}

double libisyntax_get_mpp_x(const isyntax_t* isyntax) {
    return isyntax->mpp_x;
}

double libisyntax_get_mpp_y(const isyntax_t* isyntax) {
    return isyntax->mpp_y;
}

const isyntax_image_t* libisyntax_get_label_image(const isyntax_t* isyntax) {
	return isyntax->images + isyntax->label_image_index;
}

const isyntax_image_t* libisyntax_get_macro_image(const isyntax_t* isyntax) {
	return isyntax->images + isyntax->macro_image_index;
}

const char* libisyntax_get_barcode(const isyntax_t* isyntax) {
	return isyntax->barcode;
}

const char* libisyntax_get_acquisition_datetime(const isyntax_t* isyntax) {
	return isyntax->dicom_acquisition_datetime;
}

const char* libisyntax_get_manufacturer(const isyntax_t* isyntax) {
	return isyntax->dicom_manufacturer;
}

const char* libisyntax_get_manufacturers_model_name(const isyntax_t* isyntax) {
    return isyntax->dicom_manufacturers_model_name;
}

const char* libisyntax_scale_unit(const isyntax_t* isyntax) {
    return isyntax->image_dimension_unit;
}
const char* libisyntax_get_derivation_description(const isyntax_t* isyntax) {
    return isyntax->dicom_derivation_description;
}

const char* libisyntax_get_device_serial_number(const isyntax_t* isyntax) {
    return isyntax->dicom_device_serial_number;
}

int32_t libisyntax_get_software_versions_count(const isyntax_t* isyntax) {
    return isyntax->dicom_software_versions_count;
}

const char* libisyntax_get_software_versions(const isyntax_t* isyntax, int32_t index) {
    if (index < 0 || index >= isyntax->dicom_software_versions_count) {
        return NULL;
    }
    return isyntax->dicom_software_versions[index];
}

int32_t libisyntax_get_date_of_last_calibration_count(const isyntax_t* isyntax) {
    return isyntax->dicom_date_of_last_calibration_count;
}

const char* libisyntax_get_date_of_last_calibration(const isyntax_t* isyntax, int32_t index) {
    if (index < 0 || index >= isyntax->dicom_date_of_last_calibration_count) {
        return NULL;
    }
    return isyntax->dicom_date_of_last_calibration[index];
}

int32_t libisyntax_get_time_of_last_calibration_count(const isyntax_t* isyntax) {
    return isyntax->dicom_time_of_last_calibration_count;
}

const char* libisyntax_get_time_of_last_calibration(const isyntax_t* isyntax, int32_t index) {
    if (index < 0 || index >= isyntax->dicom_time_of_last_calibration_count) {
        return NULL;
    }
    return isyntax->dicom_time_of_last_calibration[index];
}

bool libisyntax_is_lossy_image_compression(const isyntax_t* isyntax) {
    return isyntax->dicom_lossy_image_compression;
}

double libisyntax_get_lossy_image_compression_ratio(const isyntax_t* isyntax) {
    return isyntax->dicom_lossy_image_compression_ratio;
}

const char* libisyntax_get_lossy_image_compression_method(const isyntax_t* isyntax) {
    return isyntax->dicom_lossy_image_compression_method;
}

int32_t libisyntax_get_data_model_major_version(const isyntax_t* isyntax) {
	return isyntax->data_model_major_version;
}

int32_t libisyntax_get_data_model_minor_version(const isyntax_t* isyntax) {
	return isyntax->data_model_minor_version;
}

int32_t libisyntax_image_get_level_count(const isyntax_image_t* image) {
    return image->level_count;
}

int32_t libisyntax_image_get_offset_x(const isyntax_image_t* image) {
    return image->offset_x;
}

int32_t libisyntax_image_get_offset_y(const isyntax_image_t* image) {
    return image->offset_y;
}

const isyntax_level_t* libisyntax_image_get_level(const isyntax_image_t* image, int32_t index) {
    return &image->levels[index];
}

int32_t libisyntax_level_get_scale(const isyntax_level_t* level) {
    return level->scale;
}

int32_t libisyntax_level_get_width_in_tiles(const isyntax_level_t* level) {
    return level->width_in_tiles;
}

int32_t libisyntax_level_get_height_in_tiles(const isyntax_level_t* level) {
    return level->height_in_tiles;
}

int32_t libisyntax_level_get_width(const isyntax_level_t* level) {
	return level->width;
}

int32_t libisyntax_level_get_height(const isyntax_level_t* level) {
	return level->height;
}

float libisyntax_level_get_mpp_x(const isyntax_level_t* level) {
	return level->um_per_pixel_x;
}

float libisyntax_level_get_mpp_y(const isyntax_level_t* level) {
	return level->um_per_pixel_y;
}

double libisyntax_level_get_downsample_factor(const isyntax_level_t* level) {
    return level->downsample_factor;
}

double libisyntax_level_get_origin_offset_in_pixels(const isyntax_level_t* level) {
    return level->origin_offset_in_pixels;
}

// == Valid data ==
// An envelope is an orthogonal polygon, given by its vertex set. Pixels from the smallest x / y of the polygon are in
// it, up to the largest excluded.

#define ENVELOPE_MAX_VERTICES 64

typedef struct envelope_edge_t {
    i64 at;   // x of a vertical edge, y of a horizontal one
    i64 from; // span on the other axis, end excluded
    i64 to;
} envelope_edge_t;

typedef struct envelope_polygon_t {
    i64 x[ENVELOPE_MAX_VERTICES];
    i64 y[ENVELOPE_MAX_VERTICES];
    i32 count;
} envelope_polygon_t;

static i64 floor_div2(i64 v) {
    return v >= 0 ? v / 2 : -((-v + 1) / 2);
}

// First pixel of a range after `levels` wavelet levels.
static i64 erode_start(i64 v, i32 levels) {
    for (i32 i = 0; i < levels; ++i) v = -floor_div2(-v) + 1; // ceil(v / 2) + 1
    return v;
}

// Last pixel (included) of a range after `levels` wavelet levels.
static i64 erode_end(i64 v, i32 levels) {
    for (i32 i = 0; i < levels; ++i) v = floor_div2(v) - 1;
    return v;
}

static int compare_i64(const void* a, const void* b) {
    i64 x = *(const i64*)a, y = *(const i64*)b;
    return (x > y) - (x < y);
}

typedef struct envelope_vertex_t {
    i64 line; // x for a vertical edge, y for a horizontal one
    i64 pos;  // position on that line
} envelope_vertex_t;

static int compare_vertex(const void* a, const void* b) {
    const envelope_vertex_t* u = a;
    const envelope_vertex_t* v = b;
    if (u->line != v->line) return (u->line > v->line) - (u->line < v->line);
    return (u->pos > v->pos) - (u->pos < v->pos);
}

// Vertical (or horizontal) edges: the vertices of every column (row), paired in order.
static i32 envelope_edges(const envelope_polygon_t* p, bool vertical, envelope_edge_t* edges) {
    envelope_vertex_t vertices[ENVELOPE_MAX_VERTICES];
    for (i32 i = 0; i < p->count; ++i) {
        vertices[i] = vertical ? (envelope_vertex_t){p->x[i], p->y[i]} : (envelope_vertex_t){p->y[i], p->x[i]};
    }
    qsort(vertices, p->count, sizeof(envelope_vertex_t), compare_vertex);
    i32 n = 0;
    for (i32 i = 0; i + 1 < p->count; i += 2) {
        if (vertices[i].line != vertices[i + 1].line) continue; // malformed polygon
        edges[n++] = (envelope_edge_t){vertices[i].line, vertices[i].pos, vertices[i + 1].pos};
    }
    return n;
}

// Whether pixel (x, y) is in the polygon: count the vertical edges on its left.
static bool envelope_contains(const envelope_edge_t* vertical, i32 count, i64 x, i64 y) {
    i32 crossings = 0;
    for (i32 i = 0; i < count; ++i) {
        if (vertical[i].at <= x && vertical[i].from <= y && y < vertical[i].to) ++crossings;
    }
    return crossings % 2 == 1;
}

// The polygon at `level`: every edge moved as the SDK erodes it.
static void envelope_erode(const envelope_polygon_t* p, i32 level, envelope_polygon_t* out) {
    envelope_edge_t vertical[ENVELOPE_MAX_VERTICES], horizontal[ENVELOPE_MAX_VERTICES];
    i32 nv = envelope_edges(p, true, vertical);
    i32 nh = envelope_edges(p, false, horizontal);
    *out = *p;
    for (i32 e = 0; e < nv; ++e) {
        // The polygon starts at this edge when it holds the pixel on its right.
        envelope_edge_t edge = vertical[e];
        i64 x = envelope_contains(vertical, nv, edge.at, edge.from) ? erode_start(edge.at, level)
                                                                     : erode_end(edge.at - 1, level) + 1;
        for (i32 i = 0; i < p->count; ++i) {
            if (p->x[i] == edge.at && (p->y[i] == edge.from || p->y[i] == edge.to)) out->x[i] = x;
        }
    }
    for (i32 e = 0; e < nh; ++e) {
        // The polygon starts at this edge when it holds the pixel below it.
        envelope_edge_t edge = horizontal[e];
        i64 y = envelope_contains(vertical, nv, edge.from, edge.at) ? erode_start(edge.at, level)
                                                                     : erode_end(edge.at - 1, level) + 1;
        for (i32 i = 0; i < p->count; ++i) {
            if (p->y[i] == edge.at && (p->x[i] == edge.from || p->x[i] == edge.to)) out->y[i] = y;
        }
    }
}

typedef struct envelope_rects_t {
    i64* values; // x, y, width, height
    i32 count;
    i32 capacity;
} envelope_rects_t;

// Adds a strip, or extends the strip of this polygon (from index `first`) that ends where it starts over the same
// rows.
static bool envelope_add_strip(envelope_rects_t* rects, i32 first, i64 x, i64 y, i64 w, i64 h) {
    for (i32 k = first; k < rects->count; ++k) {
        i64* r = rects->values + 4 * k;
        if (r[0] + r[2] == x && r[1] == y && r[3] == h) {
            r[2] += w;
            return true;
        }
    }
    if (rects->count == rects->capacity) {
        i32 capacity = rects->capacity ? 2 * rects->capacity : 16;
        i64* grown = realloc(rects->values, 4 * (size_t)capacity * sizeof(i64));
        if (!grown) return false;
        rects->values = grown;
        rects->capacity = capacity;
    }
    i64* r = rects->values + 4 * rects->count++;
    r[0] = x; r[1] = y; r[2] = w; r[3] = h;
    return true;
}

static i32 unique_sorted(i64* values, i32 count) {
    qsort(values, count, sizeof(i64), compare_i64);
    i32 n = 0;
    for (i32 i = 0; i < count; ++i) {
        if (n == 0 || values[n - 1] != values[i]) values[n++] = values[i];
    }
    return n;
}

// Vertical strips of the polygon, cut at the x of its vertices.
static bool envelope_strips(const envelope_polygon_t* p, envelope_rects_t* rects) {
    envelope_edge_t vertical[ENVELOPE_MAX_VERTICES];
    i32 nv = envelope_edges(p, true, vertical);
    i64 xs[ENVELOPE_MAX_VERTICES], ys[2 * ENVELOPE_MAX_VERTICES];
    for (i32 e = 0; e < nv; ++e) {
        xs[e] = vertical[e].at;
        ys[2 * e] = vertical[e].from;
        ys[2 * e + 1] = vertical[e].to;
    }
    i32 nx = unique_sorted(xs, nv);
    i32 ny = unique_sorted(ys, 2 * nv);
    i32 first = rects->count;
    for (i32 i = 0; i + 1 < nx; ++i) {
        i64 run_start = -1, run_end = -1;
        for (i32 j = 0; j + 1 < ny; ++j) {
            if (envelope_contains(vertical, nv, xs[i], ys[j])) {
                if (run_start < 0 || run_end != ys[j]) {
                    if (run_start >= 0 && !envelope_add_strip(rects, first, xs[i], run_start, xs[i + 1] - xs[i],
                                                              run_end - run_start)) return false;
                    run_start = ys[j];
                }
                run_end = ys[j + 1];
            }
        }
        if (run_start >= 0 && !envelope_add_strip(rects, first, xs[i], run_start, xs[i + 1] - xs[i],
                                                  run_end - run_start)) return false;
    }
    return true;
}

// Level 0 envelopes of the WSI image, in pixels of `libisyntax_read_region()`. Returns their number.
static i32 envelope_polygons(const isyntax_t* isyntax, envelope_polygon_t** out) {
    const isyntax_image_t* wsi = isyntax->images + isyntax->wsi_image_index;
    *out = NULL;
    if (isyntax->valid_data_envelope_count > 0) {
        // iSyntax v2: polygons of the header, in pixels of the padded level 0 grid.
        envelope_polygon_t* polygons = calloc(isyntax->valid_data_envelope_count, sizeof(envelope_polygon_t));
        if (!polygons) return 0;
        for (i32 e = 0; e < isyntax->valid_data_envelope_count; ++e) {
            const isyntax_valid_data_envelope_t* envelope = isyntax->valid_data_envelopes + e;
            polygons[e].count = MIN(envelope->vertex_count, ENVELOPE_MAX_VERTICES);
            for (i32 v = 0; v < polygons[e].count; ++v) {
                polygons[e].x[v] = envelope->vertices[v].x - wsi->level0_padding;
                polygons[e].y[v] = envelope->vertices[v].y - wsi->level0_padding;
            }
        }
        *out = polygons;
        return isyntax->valid_data_envelope_count;
    }
    // iSyntax v1: the box around every group of connected level 0 tiles present.
    if (wsi->level_count <= 0 || wsi->levels[0].tiles == NULL) return 0;
    const isyntax_level_t* level0 = wsi->levels + 0;
    i64 w = level0->width_in_tiles, h = level0->height_in_tiles, n = w * h;
    u8* seen = calloc(n, 1);
    i64* queue = malloc(n * sizeof(i64));
    envelope_polygon_t* polygons = NULL;
    i32 count = 0, capacity = 0;
    for (i64 start = 0; seen && queue && start < n; ++start) {
        if (!level0->tiles[start].exists || seen[start]) continue;
        i64 tx0 = start % w, tx1 = tx0, ty0 = start / w, ty1 = ty0;
        i64 head = 0, tail = 0;
        seen[start] = 1;
        queue[tail++] = start;
        while (head < tail) {
            i64 index = queue[head++], tx = index % w, ty = index / w;
            tx0 = MIN(tx0, tx); tx1 = MAX(tx1, tx); ty0 = MIN(ty0, ty); ty1 = MAX(ty1, ty);
            i64 neighbours[4][2] = {{tx - 1, ty}, {tx + 1, ty}, {tx, ty - 1}, {tx, ty + 1}};
            for (i32 k = 0; k < 4; ++k) {
                i64 nx = neighbours[k][0], ny = neighbours[k][1];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                i64 next = ny * w + nx;
                if (level0->tiles[next].exists && !seen[next]) {
                    seen[next] = 1;
                    queue[tail++] = next;
                }
            }
        }
        i64 x0 = MAX(tx0 * isyntax->tile_width - wsi->level0_padding, 0);
        i64 y0 = MAX(ty0 * isyntax->tile_height - wsi->level0_padding, 0);
        i64 x1 = MIN((tx1 + 1) * isyntax->tile_width - wsi->level0_padding, wsi->width);
        i64 y1 = MIN((ty1 + 1) * isyntax->tile_height - wsi->level0_padding, wsi->height);
        if (x1 <= x0 || y1 <= y0) continue;
        if (count == capacity) {
            capacity = capacity ? 2 * capacity : 8;
            envelope_polygon_t* grown = realloc(polygons, capacity * sizeof(envelope_polygon_t));
            if (!grown) break;
            polygons = grown;
        }
        polygons[count++] = (envelope_polygon_t){{x0, x1, x0, x1}, {y0, y0, y1, y1}, 4};
    }
    free(seen);
    free(queue);
    *out = polygons;
    return count;
}

isyntax_error_t libisyntax_image_get_valid_range(const isyntax_image_t* image, int32_t level,
                                                 int64_t* start_x, int64_t* start_y, int64_t* end_x, int64_t* end_y) {
    if (!image || level < 0 || level >= image->level_count || !start_x || !start_y || !end_x || !end_y) {
        return LIBISYNTAX_INVALID_ARGUMENT;
    }
    *start_x = *start_y = erode_start(0, level);
    *end_x = erode_end(image->width - 1, level);
    *end_y = erode_end(image->height - 1, level);
    return LIBISYNTAX_OK;
}

isyntax_error_t libisyntax_get_data_bounds(const isyntax_t* isyntax, int64_t* x, int64_t* y,
                                           int64_t* width, int64_t* height) {
    if (!isyntax || !x || !y || !width || !height) return LIBISYNTAX_INVALID_ARGUMENT;
    const isyntax_image_t* wsi = isyntax->images + isyntax->wsi_image_index;
    envelope_polygon_t* polygons;
    i32 count = envelope_polygons(isyntax, &polygons);
    i64 x0 = INT64_MAX, y0 = INT64_MAX, x1 = INT64_MIN, y1 = INT64_MIN;
    for (i32 e = 0; e < count; ++e) {
        for (i32 v = 0; v < polygons[e].count; ++v) {
            x0 = MIN(x0, polygons[e].x[v]); x1 = MAX(x1, polygons[e].x[v]);
            y0 = MIN(y0, polygons[e].y[v]); y1 = MAX(y1, polygons[e].y[v]);
        }
    }
    free(polygons);
    x0 = MAX(x0, 0); y0 = MAX(y0, 0);
    x1 = MIN(x1, (i64)wsi->width); y1 = MIN(y1, (i64)wsi->height);
    if (x1 <= x0 || y1 <= y0) {
        x0 = y0 = 0;
        x1 = wsi->width;
        y1 = wsi->height;
    }
    *x = x0; *y = y0; *width = x1 - x0; *height = y1 - y0;
    return LIBISYNTAX_OK;
}

isyntax_error_t libisyntax_get_data_envelopes(const isyntax_t* isyntax, int32_t level,
                                              int64_t* rects, int32_t capacity, int32_t* count) {
    if (!isyntax || !count || capacity < 0 || (capacity > 0 && !rects)) return LIBISYNTAX_INVALID_ARGUMENT;
    const isyntax_image_t* wsi = isyntax->images + isyntax->wsi_image_index;
    if (level < 0 || level >= wsi->level_count) return LIBISYNTAX_INVALID_ARGUMENT;
    envelope_polygon_t* polygons;
    i32 n = envelope_polygons(isyntax, &polygons);
    envelope_rects_t regions = {0};
    bool ok = true;
    for (i32 e = 0; ok && e < n; ++e) {
        envelope_polygon_t eroded;
        envelope_erode(polygons + e, level, &eroded);
        ok = envelope_strips(&eroded, &regions);
    }
    free(polygons);
    if (ok) {
        *count = regions.count;
        for (i32 k = 0; k < regions.count && k < capacity; ++k) {
            for (i32 c = 0; c < 4; ++c) rects[4 * k + c] = regions.values[4 * k + c];
        }
    }
    free(regions.values);
    return ok ? LIBISYNTAX_OK : LIBISYNTAX_FATAL;
}

isyntax_error_t libisyntax_cache_create(const char* debug_name_or_null, int32_t cache_size,
                                        isyntax_cache_t** out_isyntax_cache)
{
    isyntax_cache_t* cache_ptr = malloc(sizeof(isyntax_cache_t));
    memset(cache_ptr, 0, sizeof(*cache_ptr));
    tile_list_init(&cache_ptr->cache_list, debug_name_or_null);
    cache_ptr->target_cache_size = cache_size;
    platform_mutex_init(&cache_ptr->mutex);

    // Note: rest of initialization is deferred to the first injection, as that is where we will know the block size.

    *out_isyntax_cache = cache_ptr;
    return LIBISYNTAX_OK;
}

isyntax_error_t libisyntax_cache_inject(isyntax_cache_t* isyntax_cache, isyntax_t* isyntax) {
    // TODO(avirodov): consider refactoring implementation to another file, here and in destroy.
    if (isyntax->ll_coeff_block_allocator != NULL || isyntax->h_coeff_block_allocator != NULL) {
        return LIBISYNTAX_INVALID_ARGUMENT;
    }

    isyntax_cache->allocator_block_width = isyntax->block_width;
    isyntax_cache->allocator_block_height = isyntax->block_height;
    size_t ll_coeff_block_size = isyntax->block_width * isyntax->block_height * sizeof(icoeff_t);
    size_t block_allocator_maximum_capacity_in_blocks = GIGABYTES(32) / ll_coeff_block_size;
    size_t ll_coeff_block_allocator_capacity_in_blocks = block_allocator_maximum_capacity_in_blocks / 4;
    size_t h_coeff_block_size = ll_coeff_block_size * 3;
    size_t h_coeff_block_allocator_capacity_in_blocks = ll_coeff_block_allocator_capacity_in_blocks * 3;
    isyntax_cache->ll_coeff_block_allocator = malloc(sizeof(block_allocator_t));
    isyntax_cache->h_coeff_block_allocator = malloc(sizeof(block_allocator_t));
    block_allocator_init(isyntax_cache->ll_coeff_block_allocator, ll_coeff_block_size, ll_coeff_block_allocator_capacity_in_blocks, MEGABYTES(256));
    block_allocator_init(isyntax_cache->h_coeff_block_allocator, h_coeff_block_size, h_coeff_block_allocator_capacity_in_blocks, MEGABYTES(256));
    isyntax_cache->is_block_allocator_owned = true;

    if (isyntax_cache->allocator_block_width != isyntax->block_width ||
            isyntax_cache->allocator_block_height != isyntax->block_height) {
        return LIBISYNTAX_FATAL; // Not implemented, see todo in libisyntax.h.
    }

    isyntax->ll_coeff_block_allocator = isyntax_cache->ll_coeff_block_allocator;
    isyntax->h_coeff_block_allocator = isyntax_cache->h_coeff_block_allocator;
    isyntax->is_block_allocator_owned = false;
    return LIBISYNTAX_OK;
}


void libisyntax_cache_flush(isyntax_cache_t* isyntax_cache, isyntax_t* isyntax_or_null) {
    // Flusing per-isyntax is not yet implemented.
    (void)isyntax_or_null;

    platform_mutex_lock(&isyntax_cache->mutex);

    while (isyntax_cache->cache_list.tail) {
        tile_list_remove(&isyntax_cache->cache_list, isyntax_cache->cache_list.tail);
    }

    platform_mutex_unlock(&isyntax_cache->mutex);
}

void libisyntax_cache_destroy(isyntax_cache_t* isyntax_cache) {
    if (isyntax_cache->is_block_allocator_owned) {
        if (isyntax_cache->ll_coeff_block_allocator->is_valid) {
            block_allocator_destroy(isyntax_cache->ll_coeff_block_allocator);
        }
        if (isyntax_cache->h_coeff_block_allocator->is_valid) {
            block_allocator_destroy(isyntax_cache->h_coeff_block_allocator);
        }
    }

    platform_mutex_destroy(&isyntax_cache->mutex);
    free(isyntax_cache);
}

// TODO(pvalkema): should we allow passing a stride for the pixels_buffer, to allow blitting into buffers
//  that are not exactly the height/width of the region?
isyntax_error_t libisyntax_tile_read(isyntax_t* isyntax, isyntax_cache_t* isyntax_cache,
                                     int32_t level, int64_t tile_x, int64_t tile_y,
                                     uint32_t* pixels_buffer, int32_t pixel_format) {
    if (pixel_format <= _LIBISYNTAX_PIXEL_FORMAT_START || pixel_format >= _LIBISYNTAX_PIXEL_FORMAT_END) {
        return LIBISYNTAX_INVALID_ARGUMENT;
    }
    // TODO(avirodov): additional vaidations, e.g. tile_x >= 0 && tile_x < isyntax...[level]...->width_in_tiles.

    // TODO(avirodov): if isyntax_cache is null, we can support using allocators that are in isyntax object,
    //  if is_init_allocators = 1 when created. Not sure is needed.
    isyntax_tile_read(isyntax, isyntax_cache, level, tile_x, tile_y, pixels_buffer, pixel_format);
    return LIBISYNTAX_OK;
}

isyntax_error_t libisyntax_read_region(isyntax_t* isyntax, isyntax_cache_t* isyntax_cache, int32_t level,
                                       int64_t x, int64_t y, int64_t width, int64_t height, uint32_t* pixels_buffer,
                                       int32_t pixel_format) {

    if (pixel_format <= _LIBISYNTAX_PIXEL_FORMAT_START || pixel_format >= _LIBISYNTAX_PIXEL_FORMAT_END) {
        return LIBISYNTAX_INVALID_ARGUMENT;
    }

    // Get the level
    ASSERT(level < isyntax->images[0].level_count);
    isyntax_level_t* current_level = &isyntax->images[0].levels[level];

    // The pyramid is padded by `level0_padding` pixels on each side of level 0, and the tile grid of each level starts
    // `origin_offset_in_pixels` (in level 0 pixels) into that padding. Rounding down to whole pixels of the level
    // gives the same pixels as the Philips SDK at every level.
    int32_t level0_padding = isyntax->images[0].level0_padding;
    int32_t origin_offset = (level == 0) ? 0 : current_level->origin_offset_in_pixels;
    int32_t offset = (level0_padding - origin_offset) >> level;

    x += offset;
    y += offset;

    int32_t tile_width = isyntax->tile_width;
    int32_t tile_height = isyntax->tile_height;

    int64_t start_tile_x;
    int64_t end_tile_x;
    int64_t x_remainder;
    int64_t x_remainder_last;

    if (x > 0) {
        start_tile_x = x / tile_width;
        end_tile_x = (x + width - 1) / tile_width;
        x_remainder = x % tile_width;
        x_remainder_last = (x + width - 1) % tile_width;
    } else {
        start_tile_x = -(-x / tile_width);
        end_tile_x = -(-(x + width - 1) / tile_width);
        x_remainder = (x % tile_width + tile_width) % tile_width;
        x_remainder_last = ((x + width - 1) % tile_width + tile_width) % tile_width;
    }

    int64_t start_tile_y;
    int64_t end_tile_y;
    int64_t y_remainder;
    int64_t y_remainder_last;

    if (y > 0) {
        start_tile_y = y / tile_height;
        end_tile_y = (y + height - 1) / tile_height;
        y_remainder = y % tile_height;
        y_remainder_last = (y + height - 1) % tile_height;
    } else {
        start_tile_y = -(-y / tile_height);
        end_tile_y = -(-(y + height - 1) / tile_height);
        y_remainder = (y % tile_height + tile_height) % tile_height;
        y_remainder_last = ((y + height - 1) % tile_height + tile_height) % tile_height;
    }

    // Allocate memory for tile pixels (will reuse for consecutive libisyntax_tile_read() calls)
    uint32_t* tile_pixels = (uint32_t*)malloc(tile_width * tile_height * sizeof(uint32_t));

    // Read tiles and copy the relevant portion of each tile to the region
    for (int64_t tile_y = start_tile_y; tile_y <= end_tile_y; ++tile_y) {
        for (int64_t tile_x = start_tile_x; tile_x <= end_tile_x; ++tile_x) {
            // Calculate the portion of the tile to be copied
            int64_t src_x = (tile_x == start_tile_x) ? x_remainder : 0;
            int64_t src_y = (tile_y == start_tile_y) ? y_remainder : 0;
            int64_t dest_x = (tile_x == start_tile_x) ? 0 : (tile_x - start_tile_x) * tile_width - x_remainder;
            int64_t dest_y = (tile_y == start_tile_y) ? 0 : (tile_y - start_tile_y) * tile_height - y_remainder;
            int64_t copy_width = (tile_x == end_tile_x) ? x_remainder_last - src_x + 1 : tile_width - src_x;
            int64_t copy_height = (tile_y == end_tile_y) ? y_remainder_last - src_y + 1 : tile_height - src_y;

            // Read tile
            CHECK_LIBISYNTAX_OK(libisyntax_tile_read(isyntax, isyntax_cache, level, tile_x, tile_y, tile_pixels, pixel_format));

            // Copy the relevant portion of the tile to the region
            for (int64_t i = 0; i < copy_height; ++i) {
                int64_t dest_index = (dest_y + i) * width + dest_x;
                int64_t src_index = (src_y + i) * tile_width + src_x;
                memcpy((pixels_buffer) + dest_index,
                       tile_pixels + src_index,
                       copy_width * sizeof(uint32_t));
            }
        }
    }

    free(tile_pixels);

    return LIBISYNTAX_OK;
}

// TODO(pvalkema): remove this / only support returning compressed JPEG buffer and leave decompression to caller?
static isyntax_error_t libisyntax_read_associated_image(isyntax_t* isyntax, isyntax_image_t* image, int32_t* width, int32_t* height,
                                                        uint32_t** pixels_buffer, int32_t pixel_format) {
    if (pixel_format <= _LIBISYNTAX_PIXEL_FORMAT_START || pixel_format >= _LIBISYNTAX_PIXEL_FORMAT_END) {
        return LIBISYNTAX_INVALID_ARGUMENT;
    }
    uint32_t* pixels = (uint32_t*)isyntax_get_associated_image_pixels(isyntax, image, pixel_format);
    // NOTE: the width and height are only known AFTER the decoding.
    if (width) *width = image->width;
    if (height) *height = image->height;
    if (pixels_buffer) *pixels_buffer = pixels;
    return LIBISYNTAX_OK;
}

isyntax_error_t libisyntax_read_label_image(isyntax_t* isyntax, int32_t* width, int32_t* height,
                                            uint32_t** pixels_buffer, int32_t pixel_format) {
    isyntax_image_t* label_image = isyntax->images + isyntax->label_image_index;
    return libisyntax_read_associated_image(isyntax, label_image, width, height, pixels_buffer, pixel_format);
}

isyntax_error_t libisyntax_read_macro_image(isyntax_t* isyntax, int32_t* width, int32_t* height,
                                            uint32_t** pixels_buffer, int32_t pixel_format) {
    isyntax_image_t* macro_image = isyntax->images + isyntax->macro_image_index;
    return libisyntax_read_associated_image(isyntax, macro_image, width, height, pixels_buffer, pixel_format);
}

static isyntax_error_t libisyntax_read_assocatiated_image_jpeg(isyntax_t* isyntax, isyntax_image_t* image, uint8_t** jpeg_buffer, uint32_t* jpeg_size) {
    ASSERT(jpeg_buffer);
    ASSERT(jpeg_size);
    u8* jpeg_compressed = isyntax_get_associated_image_jpeg(isyntax, image, jpeg_size);
    if (jpeg_compressed) {
        *jpeg_buffer = jpeg_compressed;
        return LIBISYNTAX_OK;
    } else {
        return LIBISYNTAX_FATAL;
    }
}

isyntax_error_t libisyntax_read_label_image_jpeg(isyntax_t* isyntax, uint8_t** jpeg_buffer, uint32_t* jpeg_size) {
    isyntax_image_t* label_image = isyntax->images + isyntax->label_image_index;
    return libisyntax_read_assocatiated_image_jpeg(isyntax, label_image, jpeg_buffer, jpeg_size);
}

isyntax_error_t libisyntax_read_macro_image_jpeg(isyntax_t* isyntax, uint8_t** jpeg_buffer, uint32_t* jpeg_size) {
    isyntax_image_t* macro_image = isyntax->images + isyntax->macro_image_index;
    return libisyntax_read_assocatiated_image_jpeg(isyntax, macro_image, jpeg_buffer, jpeg_size);
}

isyntax_error_t libisyntax_read_icc_profile(isyntax_t* isyntax, isyntax_image_t* image, uint8_t** icc_profile_buffer, uint32_t* icc_profile_size) {
    ASSERT(icc_profile_buffer);
    ASSERT(icc_profile_size);
    u8* icc_profile_compressed = isyntax_get_icc_profile(isyntax, image, icc_profile_size);
    if (icc_profile_compressed) {
        *icc_profile_buffer = icc_profile_compressed;
        return LIBISYNTAX_OK;
    } else {
        return LIBISYNTAX_FATAL;
    }
}
