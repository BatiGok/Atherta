/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "doctest.h"

#include "core/GutterDilation.h"

#include <vector>

using namespace atherta;

namespace {

struct Image {
    int w, h;
    std::vector<uint8_t> rgba;
    std::vector<uint8_t> coverage;

    Image(int w_, int h_) : w(w_), h(h_), rgba(size_t(w_) * h_ * 4, 0), coverage(size_t(w_) * h_, 0) {}

    uint8_t *px(int x, int y) { return &rgba[(size_t(y) * w + x) * 4]; }
    void set(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        uint8_t *p = px(x, y);
        p[0] = r;
        p[1] = g;
        p[2] = b;
        p[3] = a;
    }
};

} // namespace

TEST_CASE("alphaBounds finds the painted rectangle")
{
    Image img(16, 8);
    CHECK(alphaBounds(img.rgba.data(), img.w, img.h).isEmpty());
    img.set(3, 2, 1, 2, 3, 4);
    img.set(9, 5, 1, 2, 3, 255);
    const IRect r = alphaBounds(img.rgba.data(), img.w, img.h);
    CHECK(r.x == 3);
    CHECK(r.y == 2);
    CHECK(r.w == 7);
    CHECK(r.h == 4);
}

TEST_CASE("Dilation fills the gutter but never covered texels")
{
    // Left half (x < 8) is a UV island, right half is gutter.
    Image img(16, 4);
    for (int y = 0; y < img.h; ++y) {
        for (int x = 0; x < 8; ++x) {
            img.coverage[size_t(y) * img.w + x] = 255;
        }
    }
    // Paint the island's right edge column.
    for (int y = 0; y < img.h; ++y) {
        img.set(7, y, 200, 100, 50, 255);
    }
    const IRect changed = dilateIntoGutter(img.rgba.data(), img.w, img.h, img.coverage.data(),
                                           {7, 0, 1, 4}, 3);
    // Three texels grown into the gutter.
    for (int y = 0; y < img.h; ++y) {
        for (int x = 8; x <= 10; ++x) {
            CHECK(img.px(x, y)[3] == 255);
            CHECK(img.px(x, y)[0] == 200);
        }
        CHECK(img.px(11, y)[3] == 0);
        // Covered but unpainted texels stay untouched.
        CHECK(img.px(6, y)[3] == 0);
    }
    CHECK(changed.x <= 8);
    CHECK(changed.right() >= 11);
}

TEST_CASE("Dilation stops early when there is nothing to grow into")
{
    Image img(4, 4);
    std::fill(img.coverage.begin(), img.coverage.end(), 255);
    img.set(1, 1, 10, 10, 10, 255);
    dilateIntoGutter(img.rgba.data(), img.w, img.h, img.coverage.data(), {1, 1, 1, 1}, 8);
    int painted = 0;
    for (int i = 0; i < 16; ++i) {
        painted += img.rgba[size_t(i) * 4 + 3] != 0;
    }
    CHECK(painted == 1);
}
