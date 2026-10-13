/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstdint>

namespace atherta {

struct IRect {
    int x = 0, y = 0, w = 0, h = 0;

    bool isEmpty() const { return w <= 0 || h <= 0; }
    int right() const { return x + w; }   // exclusive
    int bottom() const { return y + h; }  // exclusive
};

/** Smallest rectangle containing all texels with non-zero alpha (RGBA8 buffer). */
IRect alphaBounds(const uint8_t *rgba, int width, int height);

/**
 * Grows painted texels outwards into the UV gutter, i.e. texels that no UV
 * triangle covers. Texels covered by the mesh are never modified, so paint
 * cannot bleed onto neighbouring UV islands.
 *
 * @param rgba      premultiplied RGBA8 texels, row-major, width * height
 * @param coverage  one byte per texel, non-zero where a UV triangle covers it
 * @param region    area that contains the painted texels
 * @param iterations how many texels to grow (padding width)
 * @return the region that may have changed (region grown by @p iterations)
 */
IRect dilateIntoGutter(uint8_t *rgba, int width, int height, const uint8_t *coverage,
                       IRect region, int iterations);

} // namespace atherta
