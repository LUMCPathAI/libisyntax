// Valid range, bounds and envelopes of testslide.isyntax against the Philips SDK 5.1 (`DimensionRanges`,
// `envelopesAsRects`).
#include <stdio.h>
#include <stdint.h>
#include "libisyntax.h"

static int failures = 0;

#define EXPECT_EQ(actual, expected) do { \
    long long a_ = (long long)(actual), e_ = (long long)(expected); \
    if (a_ != e_) { fprintf(stderr, "%s:%d: %s = %lld, expected %lld\n", __FILE__, __LINE__, #actual, a_, e_); ++failures; } \
} while (0)

static void expect_level(isyntax_t* isyntax, const isyntax_image_t* wsi, int32_t level, const int64_t range[4],
                         const int64_t envelope[4]) {
    int64_t sx, sy, ex, ey;
    EXPECT_EQ(libisyntax_image_get_valid_range(wsi, level, &sx, &sy, &ex, &ey), LIBISYNTAX_OK);
    EXPECT_EQ(sx, range[0]);
    EXPECT_EQ(sy, range[1]);
    EXPECT_EQ(ex, range[2]);
    EXPECT_EQ(ey, range[3]);

    int32_t count = -1;
    EXPECT_EQ(libisyntax_get_data_envelopes(isyntax, level, NULL, 0, &count), LIBISYNTAX_OK);
    EXPECT_EQ(count, 1);
    int64_t rect[4] = {0};
    EXPECT_EQ(libisyntax_get_data_envelopes(isyntax, level, rect, 1, &count), LIBISYNTAX_OK);
    for (int i = 0; i < 4; ++i) EXPECT_EQ(rect[i], envelope[i]);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s testslide.isyntax\n", argv[0]);
        return 1;
    }
    if (libisyntax_init() != LIBISYNTAX_OK) return 1;
    isyntax_t* isyntax = NULL;
    if (libisyntax_open(argv[1], 0, &isyntax) != LIBISYNTAX_OK) return 1;
    const isyntax_image_t* wsi = libisyntax_get_wsi_image(isyntax);

    // The slide is 37382 x 73222; its level 0 tiles hold data up to x = 37122.
    int64_t x, y, width, height;
    EXPECT_EQ(libisyntax_get_data_bounds(isyntax, &x, &y, &width, &height), LIBISYNTAX_OK);
    EXPECT_EQ(x, 0);
    EXPECT_EQ(y, 0);
    EXPECT_EQ(width, 37123);
    EXPECT_EQ(height, 73222);

    // Valid range (start x, start y, end x, end y, included) and envelope (x, y, width, height), in level pixels.
    expect_level(isyntax, wsi, 0, (int64_t[]){0, 0, 37381, 73221}, (int64_t[]){0, 0, 37123, 73222});
    expect_level(isyntax, wsi, 1, (int64_t[]){1, 1, 18689, 36609}, (int64_t[]){1, 1, 18560, 36609});
    expect_level(isyntax, wsi, 4, (int64_t[]){2, 2, 2334, 4574}, (int64_t[]){2, 2, 2317, 4573});

    int32_t count;
    EXPECT_EQ(libisyntax_get_data_envelopes(isyntax, -1, NULL, 0, &count), LIBISYNTAX_INVALID_ARGUMENT);

    libisyntax_close(isyntax);
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
