/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "GutterDilation.h"
#include "Math.h"
#include "Mesh.h"

namespace atherta {

/**
 * Texture area (in texels, row 0 = top / V = 1) that a screen-space stroke
 * can reach: the union of the UV bounds of every triangle whose screen
 * bounds intersect @p screenRect. Used to limit baking and read-back.
 *
 * @param screenRect stroke bounds in viewport pixels (Y down)
 * @param padding    extra texels around the result (for gutter dilation)
 */
IRect textureRegionForScreenRect(const Mesh &mesh, const Mat4 &viewProj, int viewportWidth,
                                 int viewportHeight, const IRect &screenRect, int textureWidth,
                                 int textureHeight, int padding);

} // namespace atherta
