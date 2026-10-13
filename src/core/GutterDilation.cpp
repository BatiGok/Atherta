/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "GutterDilation.h"

#include <algorithm>
#include <vector>

namespace atherta {

IRect alphaBounds(const uint8_t *rgba, int width, int height)
{
    int minX = width, minY = height, maxX = -1, maxY = -1;
    for (int y = 0; y < height; ++y) {
        const uint8_t *row = rgba + static_cast<size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x) {
            if (row[x * 4 + 3] != 0) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
    }
    if (maxX < 0) {
        return IRect();
    }
    return {minX, minY, maxX - minX + 1, maxY - minY + 1};
}

IRect dilateIntoGutter(uint8_t *rgba, int width, int height, const uint8_t *coverage,
                       IRect region, int iterations)
{
    if (region.isEmpty() || iterations <= 0) {
        return region;
    }

    IRect current = region;
    std::vector<uint8_t> next;

    for (int it = 0; it < iterations; ++it) {
        // The texels that can change in this pass: one ring around `current`.
        const int x0 = std::max(0, current.x - 1);
        const int y0 = std::max(0, current.y - 1);
        const int x1 = std::min(width, current.right() + 1);
        const int y1 = std::min(height, current.bottom() + 1);
        const int w = x1 - x0;
        const int h = y1 - y0;

        next.assign(static_cast<size_t>(w) * h * 4, 0);
        bool changed = false;

        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const size_t idx = (static_cast<size_t>(y) * width + x);
                uint8_t *out = &next[(static_cast<size_t>(y - y0) * w + (x - x0)) * 4];
                const uint8_t *src = rgba + idx * 4;
                if (src[3] != 0 || coverage[idx] != 0) {
                    std::copy(src, src + 4, out);
                    continue;
                }
                // Average the painted 8-neighbours.
                int sum[4] = {0, 0, 0, 0};
                int count = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    const int ny = y + dy;
                    if (ny < 0 || ny >= height) {
                        continue;
                    }
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx;
                        if ((dx == 0 && dy == 0) || nx < 0 || nx >= width) {
                            continue;
                        }
                        const uint8_t *n = rgba + (static_cast<size_t>(ny) * width + nx) * 4;
                        if (n[3] != 0) {
                            for (int c = 0; c < 4; ++c) {
                                sum[c] += n[c];
                            }
                            ++count;
                        }
                    }
                }
                if (count > 0) {
                    for (int c = 0; c < 4; ++c) {
                        out[c] = static_cast<uint8_t>((sum[c] + count / 2) / count);
                    }
                    changed = changed || out[3] != 0;
                } else {
                    std::fill(out, out + 4, 0);
                }
            }
        }

        for (int y = y0; y < y1; ++y) {
            std::copy(&next[static_cast<size_t>(y - y0) * w * 4],
                      &next[static_cast<size_t>(y - y0) * w * 4] + static_cast<size_t>(w) * 4,
                      rgba + (static_cast<size_t>(y) * width + x0) * 4);
        }
        current = {x0, y0, w, h};
        if (!changed) {
            break;
        }
    }
    return current;
}

} // namespace atherta
