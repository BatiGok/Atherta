/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Behaviour ported from Blender (GPL-2.0-or-later):
 *   source/blender/editors/space_view3d/view3d_navigate*.cc
 *   source/blender/editors/space_view3d/view3d_utils.cc
 *   source/blender/blenkernel/intern/camera.cc
 */

#include "ViewNavigator.h"

#include <array>
#include <utility>

namespace atherta {

namespace {

constexpr float kSqrt1_2 = 0.70710678118654752440f;

// Blender: view3d_quat_axis[6][4][4] in view3d_utils.cc, (w, x, y, z).
const std::array<std::array<Quat, 4>, 6> kAxisQuats = {{
    // Front
    {{{kSqrt1_2, -kSqrt1_2, 0, 0}, {0.5f, -0.5f, -0.5f, 0.5f}, {0, 0, -kSqrt1_2, kSqrt1_2},
      {-0.5f, 0.5f, -0.5f, 0.5f}}},
    // Back
    {{{0, 0, -kSqrt1_2, -kSqrt1_2}, {0.5f, 0.5f, -0.5f, -0.5f}, {kSqrt1_2, kSqrt1_2, 0, 0},
      {0.5f, 0.5f, 0.5f, 0.5f}}},
    // Left
    {{{0.5f, -0.5f, 0.5f, 0.5f}, {0, -kSqrt1_2, 0, kSqrt1_2}, {-0.5f, -0.5f, -0.5f, 0.5f},
      {-kSqrt1_2, 0, -kSqrt1_2, 0}}},
    // Right
    {{{0.5f, -0.5f, -0.5f, -0.5f}, {kSqrt1_2, 0, -kSqrt1_2, 0}, {0.5f, 0.5f, -0.5f, 0.5f},
      {0, kSqrt1_2, 0, kSqrt1_2}}},
    // Top
    {{{1, 0, 0, 0}, {kSqrt1_2, 0, 0, kSqrt1_2}, {0, 0, 0, 1}, {-kSqrt1_2, 0, 0, kSqrt1_2}}},
    // Bottom
    {{{0, -1, 0, 0}, {0, -kSqrt1_2, -kSqrt1_2, 0}, {0, 0, -1, 0}, {0, kSqrt1_2, -kSqrt1_2, 0}}},
}};

AxisView oppositeAxisView(AxisView v)
{
    switch (v) {
    case AxisView::Front: return AxisView::Back;
    case AxisView::Back: return AxisView::Front;
    case AxisView::Left: return AxisView::Right;
    case AxisView::Right: return AxisView::Left;
    case AxisView::Top: return AxisView::Bottom;
    case AxisView::Bottom: return AxisView::Top;
    case AxisView::User: break;
    }
    return AxisView::User;
}

/** Blender: ED_view3d_quat_to_axis_view. */
bool quatToAxisView(const Quat &q, float epsilon, AxisView &outView, int &outRoll)
{
    for (int v = 0; v < 6; ++v) {
        for (int r = 0; r < 4; ++r) {
            if (angleBetween(q, kAxisQuats[v][r]) < epsilon) {
                outView = static_cast<AxisView>(v + 1);
                outRoll = r;
                return true;
            }
        }
    }
    return false;
}

/** Shortest rotation taking unit vector @p a onto unit vector @p b. */
Quat rotationBetween(const Vec3 &a, const Vec3 &b)
{
    Vec3 axis = cross(a, b);
    const float angle = angleNormalized(a, b);
    if (axis.lengthSquared() < 1e-12f) {
        if (dot(a, b) > 0.0f) {
            return Quat();
        }
        // Opposite vectors: any perpendicular axis works.
        axis = std::fabs(a.x) < 0.9f ? cross(a, Vec3(1, 0, 0)) : cross(a, Vec3(0, 1, 0));
    }
    return Quat::fromAxisAngle(axis, angle);
}

float angleWrap(float a)
{
    // Blender: angle_wrap_rad() = mod_f_positive(angle + pi, 2 pi) - pi.
    float m = std::fmod(a + kPi, 2.0f * kPi);
    if (m < 0.0f) {
        m += 2.0f * kPi;
    }
    return m - kPi;
}

/** The orientation of the view's "tan(fov/2)" along the larger viewport axis. */
constexpr float kTanHalfFov =
    ViewNavigator::kSensorWidth * ViewNavigator::kViewportZoom / (2.0f * ViewNavigator::kLens);

} // namespace

Quat axisViewQuat(AxisView view, int roll)
{
    if (view == AxisView::User) {
        return Quat();
    }
    return kAxisQuats[static_cast<int>(view) - 1][roll & 3];
}

Quat quatFromViewBasis(const Vec3 &x, const Vec3 &y, const Vec3 &z)
{
    // Rotation matrix with rows x, y, z (world -> view).
    const float m00 = x.x, m01 = x.y, m02 = x.z;
    const float m10 = y.x, m11 = y.y, m12 = y.z;
    const float m20 = z.x, m21 = z.y, m22 = z.z;
    const float tr = m00 + m11 + m22;
    Quat q;
    if (tr > 0.0f) {
        const float s = std::sqrt(tr + 1.0f) * 2.0f;
        q = {0.25f * s, (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s};
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q = {(m21 - m12) / s, 0.25f * s, (m01 + m10) / s, (m02 + m20) / s};
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q = {(m02 - m20) / s, (m01 + m10) / s, 0.25f * s, (m12 + m21) / s};
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q = {(m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, 0.25f * s};
    }
    return q.normalized();
}

ViewNavigator::ViewNavigator()
{
    // Look from the same side as Blender's default camera.
    const Vec3 z = normalized(Vec3(7.36f, -6.93f, 4.96f));
    const Vec3 x = normalized(cross(Vec3(0, 0, 1), z));
    const Vec3 y = cross(z, x);
    m_state.viewQuat = quatFromViewBasis(x, y, z);
    m_state.dist = 10.0f;
}

void ViewNavigator::setState(const ViewState &state)
{
    m_anim.active = false;
    m_state = state;
    m_state.viewQuat = m_state.viewQuat.normalized();
}

void ViewNavigator::setViewportSize(int width, int height)
{
    m_width = std::max(1, width);
    m_height = std::max(1, height);
}

// ---------------------------------------------------------------------------
// Matrices

Mat4 ViewNavigator::viewMatrix() const
{
    // Blender: view3d_viewmatrix_set():
    //   viewmat = quat_to_mat4(viewquat); viewmat[3][2] -= dist; translate(ofs)
    return Mat4::translation(Vec3(0, 0, -m_state.dist)) * Mat4::fromQuat(m_state.viewQuat) *
           Mat4::translation(-m_state.pivot);
}

float ViewNavigator::nearClip() const
{
    if (m_state.projection == Projection::Perspective) {
        return kClipStart;
    }
    // Blender uses [-clip_end / 2, clip_end / 2] for orthographic views.
    // Centre that range on the pivot so large distances never clip the model.
    return m_state.dist - kClipEnd * 0.5f;
}

float ViewNavigator::farClip() const
{
    if (m_state.projection == Projection::Perspective) {
        return kClipEnd;
    }
    return m_state.dist + kClipEnd * 0.5f;
}

Mat4 ViewNavigator::projectionMatrix() const
{
    // Blender: BKE_camera_params_compute_viewplane() with sensor fit AUTO,
    // i.e. the field of view applies to the larger viewport dimension.
    const float maxDim = static_cast<float>(std::max(m_width, m_height));
    const float sx = static_cast<float>(m_width) / maxDim;
    const float sy = static_cast<float>(m_height) / maxDim;

    if (m_state.projection == Projection::Perspective) {
        const float n = kClipStart;
        const float hx = kTanHalfFov * n * sx;
        const float hy = kTanHalfFov * n * sy;
        return Mat4::frustum(-hx, hx, -hy, hy, n, kClipEnd);
    }
    // ortho_scale = dist * sensor / lens, times the viewport zoom factor.
    const float hx = kTanHalfFov * m_state.dist * sx;
    const float hy = kTanHalfFov * m_state.dist * sy;
    return Mat4::ortho(-hx, hx, -hy, hy, nearClip(), farClip());
}

Vec3 ViewNavigator::viewDirection() const
{
    return m_state.viewQuat.conjugated().rotate(Vec3(0, 0, -1));
}

Vec3 ViewNavigator::eyePosition() const
{
    return m_state.pivot + m_state.viewQuat.conjugated().rotate(Vec3(0, 0, m_state.dist));
}

Ray ViewNavigator::rayThroughPixel(const Vec2 &pixel) const
{
    const Mat4 inv = viewProjectionMatrix().inverted();
    const float nx = 2.0f * pixel.x / static_cast<float>(m_width) - 1.0f;
    const float ny = 1.0f - 2.0f * pixel.y / static_cast<float>(m_height);
    const Vec3 nearPt = inv.transformPoint(Vec3(nx, ny, -1.0f));
    const Vec3 farPt = inv.transformPoint(Vec3(nx, ny, 1.0f));
    return {nearPt, normalized(farPt - nearPt)};
}

Vec2 ViewNavigator::worldToPixel(const Vec3 &world) const
{
    const Vec4 clip = viewProjectionMatrix() * Vec4(world, 1.0f);
    const float w = clip.w != 0.0f ? clip.w : 1.0f;
    const float nx = clip.x / w;
    const float ny = clip.y / w;
    return {(nx + 1.0f) * 0.5f * static_cast<float>(m_width),
            (1.0f - ny) * 0.5f * static_cast<float>(m_height)};
}

// ---------------------------------------------------------------------------
// Blender helpers

Vec2 ViewNavigator::toBlender(const Vec2 &qtPos) const
{
    return {qtPos.x, static_cast<float>(m_height) - qtPos.y};
}

float ViewNavigator::zfacAt(const Vec3 &world) const
{
    // Blender: ED_view3d_calc_zfac().
    float zfac = (viewProjectionMatrix() * Vec4(world, 1.0f)).w;
    if (zfac < 1e-6f && zfac > -1e-6f) {
        zfac = 1.0f;
    }
    return std::fabs(zfac);
}

Vec3 ViewNavigator::winToDelta(const Vec2 &blenderDelta, float zfac) const
{
    // Blender: ED_view3d_win_to_delta(..., precise = true).
    const float dx = 2.0f * blenderDelta.x * zfac / static_cast<float>(m_width);
    const float dy = 2.0f * blenderDelta.y * zfac / static_cast<float>(m_height);
    const Mat4 winInv = projectionMatrix().inverted();
    const Vec3 viewDelta(winInv.at(0, 0) * dx + winInv.at(0, 1) * dy,
                         winInv.at(1, 0) * dx + winInv.at(1, 1) * dy,
                         winInv.at(2, 0) * dx + winInv.at(2, 1) * dy);
    return m_state.viewQuat.conjugated().rotate(viewDelta);
}

float ViewNavigator::distMin() const
{
    // ED_view3d_dist_soft_min_get(v3d, false) = grid * 0.001
    return kGrid * 0.001f;
}

float ViewNavigator::distMax() const
{
    return kClipEnd * 10.0f;
}

void ViewNavigator::zoomToWindow(float dfac, const Vec2 *qtPos)
{
    // Blender: view_zoom_to_window_xy_3d().
    const float distNew = m_state.dist * dfac;
    if (qtPos) {
        const Vec2 b = toBlender(*qtPos);
        const Vec2 xyDelta(b.x - static_cast<float>(m_width) * 0.5f,
                           b.y - static_cast<float>(m_height) * 0.5f);
        const float zfac = zfacAt(m_state.pivot);
        const Vec3 dvec = winToDelta(xyDelta, zfac);
        // ofs = -(pivot + dvec) + dvec * dfac  =>  pivot += dvec * (1 - dfac)
        m_state.pivot += dvec * (1.0f - dfac);
    }
    m_state.dist = distNew;
}

// ---------------------------------------------------------------------------
// Rotate

void ViewNavigator::ensurePerspectiveForRotate()
{
    // Blender: ED_view3d_persp_ensure() for the rotate operator.
    if (m_prefs.autoPerspective && m_state.projection == Projection::Orthographic &&
        m_state.axisView != AxisView::User) {
        m_state.projection = Projection::Perspective;
    }
}

Vec3 ViewNavigator::trackballVector(const Vec2 &qtPos) const
{
    // Blender: calctrackballvec().
    const Vec2 p = toBlender(qtPos);
    const float radius = kTrackballSize;
    const float t = radius / std::sqrt(2.0f);
    const float sizeX = static_cast<float>(m_width);
    const float sizeY = static_cast<float>(m_height);
    const float sizeMin = std::min(sizeX, sizeY);
    Vec3 dir;
    dir.x = (p.x - sizeX * 0.5f) / (sizeMin / 2.0f);
    dir.y = (p.y - sizeY * 0.5f) / (sizeMin / 2.0f);
    const float d = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (d < t) {
        dir.z = std::sqrt(radius * radius - d * d);
    } else {
        dir.z = t * t / d;
    }
    return dir;
}

void ViewNavigator::beginRotate(const Vec2 &pos)
{
    finishAnimation();
    m_drag = Drag::Rotate;
    m_d.initPos = m_d.prevPos = pos;
    ensurePerspectiveForRotate();
    m_d.perspAfterEnsure = m_state.projection;
    m_d.initQuat = m_state.viewQuat;
    m_d.trackVec = trackballVector(pos);
    // Blender: reverse orbit direction when the world Z axis points down on screen.
    m_d.reverse = m_state.viewQuat.rotate(Vec3(0, 0, 1)).y < 0.0f ? -1.0f : 1.0f;
}

void ViewNavigator::updateRotate(const Vec2 &pos, bool snapToAxis)
{
    if (m_drag != Drag::Rotate) {
        return;
    }
    m_state.axisView = AxisView::User;

    if (m_prefs.orbitMethod == OrbitMethod::Trackball) {
        const Vec3 newVec = trackballVector(pos);
        const Vec3 dvec = newVec - m_d.trackVec;
        float angle = (dvec.length() / (2.0f * kTrackballSize)) * kPi;
        angle *= m_prefs.trackballSensitivity;
        angle = angleWrap(angle);
        const Vec3 axis = cross(m_d.trackVec, newVec);
        m_state.viewQuat = Quat::fromAxisAngle(axis, angle) * m_d.initQuat;
    } else {
        const float sensitivity = m_prefs.turntableSensitivity / m_prefs.uiScale;
        const Quat inv = m_state.viewQuat.conjugated();
        const Vec3 invX = inv.rotate(Vec3(1, 0, 0));
        const Vec3 invZ = inv.rotate(Vec3(0, 0, 1));
        const Vec3 zGlobal(0, 0, 1);

        // Blend between the rotated and un-rotated horizon to escape gimbal lock.
        Vec3 xAxis;
        if ((zGlobal - invZ).lengthSquared() > 0.001f) {
            xAxis = cross(zGlobal, invZ);
            if (dot(xAxis, invX) < 0.0f) {
                xAxis = -xAxis;
            }
            float fac = angleNormalized(zGlobal, invZ) / kPi;
            fac = std::fabs(fac - 0.5f) * 2.0f;
            fac = fac * fac;
            xAxis = lerp(xAxis, invX, fac);
        } else {
            xAxis = invX;
        }

        // Blender's Y axis points up: -(y - prev_y) in Blender == (y - prev_y) in Qt.
        const Quat quatLocalX = Quat::fromAxisAngle(xAxis, sensitivity * (pos.y - m_d.prevPos.y));
        const Quat q = m_state.viewQuat * quatLocalX;
        const Quat quatGlobalZ =
            Quat::fromAxisAngle(zGlobal, sensitivity * m_d.reverse * (pos.x - m_d.prevPos.x));
        m_state.viewQuat = q * quatGlobalZ;
    }

    m_state.viewQuat = m_state.viewQuat.normalized();

    if (snapToAxis) {
        applyRotateSnap();
    } else if (m_prefs.autoPerspective) {
        m_state.projection = m_d.perspAfterEnsure;
    }
    m_d.prevPos = pos;
}

void ViewNavigator::applyRotateSnap()
{
    // Blender: viewrotate_apply_snap(). Snap the view axis to the nearest of the
    // 26 axis/diagonal directions within 15 degrees, then snap the roll to 45
    // degree steps when that is within 15 degrees too.
    const float axisLimit = deg2rad(45.0f / 3.0f);
    const Quat inv = m_state.viewQuat.conjugated();
    const Vec3 zAxis = normalized(inv.rotate(Vec3(0, 0, 1)));

    bool found = false;
    Vec3 zBest;
    for (int x = -1; x < 2; ++x) {
        for (int y = -1; y < 2; ++y) {
            for (int z = -1; z < 2; ++z) {
                if (x || y || z) {
                    const Vec3 test = normalized(Vec3(float(x), float(y), float(z)));
                    if (angleNormalized(test, zAxis) < axisLimit) {
                        zBest = test;
                        found = true;
                    }
                }
            }
        }
    }
    if (!found) {
        if (m_prefs.autoPerspective) {
            m_state.projection = m_d.perspAfterEnsure;
        }
        return;
    }

    // The current view, minimally rotated so its Z axis matches zBest.
    const Quat align = rotationBetween(zAxis, zBest);
    const Vec3 curX = normalized(align.rotate(inv.rotate(Vec3(1, 0, 0))));
    const Vec3 curY = cross(zBest, curX);
    const Quat viewQuatAlign = quatFromViewBasis(curX, curY, zBest);

    // Candidate rolls in 45 degree steps around zBest.
    const Vec3 ref = std::fabs(zBest.z) < 0.99f ? Vec3(0, 0, 1) : Vec3(0, 1, 0);
    const Vec3 x0 = normalized(cross(ref, zBest));
    float bestAngle = axisLimit;
    bool rollFound = false;
    Quat best = viewQuatAlign;
    for (int j = 0; j < 8; ++j) {
        const Quat roll = Quat::fromAxisAngle(zBest, float(j) * deg2rad(45.0f));
        const Vec3 cx = normalized(roll.rotate(x0));
        const float angle = angleNormalized(cx, curX);
        if (angle <= bestAngle) {
            bestAngle = angle;
            rollFound = true;
            best = quatFromViewBasis(cx, cross(zBest, cx), zBest);
        }
    }

    m_state.viewQuat = best;
    if (rollFound) {
        AxisView view;
        int roll;
        if (quatToAxisView(best, 0.01f, view, roll)) {
            m_state.axisView = view;
            m_state.axisRoll = roll;
            m_state.viewQuat = axisViewQuat(view, roll);
        }
    }

    if (m_prefs.autoPerspective && m_state.axisView != AxisView::User &&
        m_state.projection == Projection::Perspective) {
        m_state.projection = Projection::Orthographic;
    }
}

// ---------------------------------------------------------------------------
// Pan

void ViewNavigator::beginPan(const Vec2 &pos)
{
    finishAnimation();
    m_drag = Drag::Pan;
    m_d.initPos = m_d.prevPos = pos;
    m_d.initZfac = zfacAt(m_state.pivot);
}

void ViewNavigator::updatePan(const Vec2 &pos)
{
    if (m_drag != Drag::Pan) {
        return;
    }
    // Blender: viewmove_apply(). event_ofs = prev - current (Y up).
    const Vec2 prevB = toBlender(m_d.prevPos);
    const Vec2 curB = toBlender(pos);
    const Vec2 eventOfs = prevB - curB;
    m_state.pivot += winToDelta(eventOfs, m_d.initZfac);
    m_d.prevPos = pos;
}

// ---------------------------------------------------------------------------
// Zoom

void ViewNavigator::beginZoom(const Vec2 &pos, double timeSeconds)
{
    finishAnimation();
    m_drag = Drag::Zoom;
    m_d.initPos = m_d.prevPos = pos;
    m_d.initDist = m_state.dist;
    m_d.lastTime = timeSeconds;
}

float ViewNavigator::zoomScaleValue(const Vec2 &curr, double now)
{
    // Blender: viewzoom_scale_value().
    const float s = m_prefs.uiScale;
    const Vec2 &init = m_d.initPos;
    const float val = m_state.dist;
    const float valOrig = m_d.initDist;

    switch (m_prefs.zoomStyle) {
    case ZoomStyle::Continue: {
        const float timeStep = static_cast<float>(now - m_d.lastTime);
        // Blender: xy_init - xy_curr with Y up.
        float fac = m_prefs.horizontalZoom ? (init.x - curr.x) : (curr.y - init.y);
        fac /= s;
        if (m_prefs.invertZoomDirection) {
            fac = -fac;
        }
        m_d.lastTime = now;
        return 1.0f + ((fac / 20.0f) * timeStep);
    }
    case ZoomStyle::Scale: {
        const Vec2 ctr(static_cast<float>(m_width) * 0.5f, static_cast<float>(m_height) * 0.5f);
        const float lenNew = (5.0f * s) + ((curr - ctr).length() / s);
        const float lenOld = (5.0f * s) + ((init - ctr).length() / s);
        return valOrig * (lenOld / std::max(lenNew, 1.0f)) / val;
    }
    case ZoomStyle::Dolly:
        break;
    }

    float lenNew = 5.0f * s;
    float lenOld = 5.0f * s;
    if (m_prefs.horizontalZoom) {
        lenNew += (static_cast<float>(m_width) - curr.x) / s;
        lenOld += (static_cast<float>(m_width) - init.x) / s;
    } else {
        // winrct.ymax - y (Blender, Y up) is the distance from the top edge.
        lenNew += curr.y / s;
        lenOld += init.y / s;
    }
    if (m_prefs.invertZoomDirection) {
        std::swap(lenNew, lenOld);
    }
    return valOrig * (2.0f * ((lenNew / std::max(lenOld, 1.0f)) - 1.0f) + 1.0f) / val;
}

void ViewNavigator::updateZoom(const Vec2 &pos, double timeSeconds)
{
    if (m_drag != Drag::Zoom) {
        return;
    }
    // Blender: viewzoom_apply_3d().
    float zfac = zoomScaleValue(pos, timeSeconds);
    if (zfac != 1.0f) {
        const float zfacMin = distMin() / m_state.dist;
        const float zfacMax = distMax() / m_state.dist;
        zfac = std::clamp(zfac, zfacMin, zfacMax);
        zoomToWindow(zfac, m_prefs.zoomToMousePosition ? &m_d.initPos : nullptr);
    }
    m_state.dist = std::clamp(m_state.dist, distMin(), distMax());
    m_d.prevPos = pos;
}

void ViewNavigator::zoomStep(int steps, const Vec2 *cursor)
{
    // Blender: view_zoom_apply_step(). Positive delta zooms in.
    finishAnimation();
    const Vec2 *zoomPos = m_prefs.zoomToMousePosition ? cursor : nullptr;
    const int count = std::abs(steps);
    for (int i = 0; i < count; ++i) {
        if (steps < 0) {
            if (m_state.dist < distMax()) {
                zoomToWindow(kWheelZoomStep, zoomPos);
            }
        } else {
            if (m_state.dist > distMin()) {
                zoomToWindow(1.0f / kWheelZoomStep, zoomPos);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Dolly

void ViewNavigator::beginDolly(const Vec2 &pos)
{
    finishAnimation();
    m_drag = Drag::Dolly;
    m_d.initPos = m_d.prevPos = pos;
    m_d.initPivot = m_state.pivot;
    // Blender: mousevec = -viewinv[2] (into the screen).
    m_d.dollyDir = viewDirection();
}

void ViewNavigator::updateDolly(const Vec2 &pos)
{
    if (m_drag != Drag::Dolly) {
        return;
    }
    // Blender: viewdolly_apply().
    float len1, len2;
    if (m_prefs.horizontalZoom) {
        len1 = (static_cast<float>(m_width) - pos.x) + 5.0f;
        len2 = (static_cast<float>(m_width) - m_d.initPos.x) + 5.0f;
    } else {
        len1 = pos.y + 5.0f;
        len2 = m_d.initPos.y + 5.0f;
    }
    if (m_prefs.invertZoomDirection) {
        std::swap(len1, len2);
    }
    const float zfac = 1.0f + ((len1 - len2) * 0.01f * m_state.dist);
    // view_dolly_to_vector_3d: ofs = orig_ofs - dvec * (1 - dfac)
    m_state.pivot = m_d.initPivot + m_d.dollyDir * (1.0f - zfac);
    m_d.prevPos = pos;
}

void ViewNavigator::dollyStep(int steps)
{
    // Blender: viewdolly_exec() uses factors 1.8 (out) and 0.2 (in).
    finishAnimation();
    const Vec3 dir = viewDirection();
    const int count = std::abs(steps);
    for (int i = 0; i < count; ++i) {
        const float dfac = steps < 0 ? 1.8f : 0.2f;
        m_state.pivot += dir * (1.0f - dfac);
    }
}

void ViewNavigator::endDrag()
{
    m_drag = Drag::None;
}

// ---------------------------------------------------------------------------
// Keyboard / numpad

void ViewNavigator::setAxisView(AxisView view, double nowSeconds)
{
    if (view == AxisView::User) {
        return;
    }
    // Blender: axis_set_view().
    finishAnimation();
    ViewState target = m_state;
    target.viewQuat = axisViewQuat(view, 0);
    target.axisView = view;
    target.axisRoll = 0;
    if (m_prefs.autoPerspective) {
        // The projection switches immediately, the rotation animates.
        m_state.projection = Projection::Orthographic;
        target.projection = Projection::Orthographic;
    }
    startSmoothView(target, nowSeconds, true);
}

void ViewNavigator::orbitStep(Direction dir, double nowSeconds)
{
    // Blender: vieworbit_exec().
    finishAnimation();
    float angle = m_prefs.numpadRotationStep;
    Quat quatMul;
    if (dir == Direction::Left || dir == Direction::Right) {
        if (dir == Direction::Right) {
            angle = -angle;
        }
        quatMul = Quat::fromAxisAngle(Vec3(0, 0, 1), angle);
    } else {
        if (dir == Direction::Down) {
            angle = -angle;
        }
        const Vec3 viewInvX = m_state.viewQuat.conjugated().rotate(Vec3(1, 0, 0));
        quatMul = Quat::fromAxisAngle(viewInvX, angle);
    }
    ViewState target = m_state;
    target.viewQuat = (m_state.viewQuat * quatMul).normalized();
    target.axisView = AxisView::User;
    startSmoothView(target, nowSeconds, true);
}

void ViewNavigator::orbitOpposite(double nowSeconds)
{
    // Blender: view3d.view_orbit with angle = pi, type = ORBITRIGHT.
    finishAnimation();
    const Quat quatMul = Quat::fromAxisAngle(Vec3(0, 0, 1), -kPi);
    ViewState target = m_state;
    target.viewQuat = (m_state.viewQuat * quatMul).normalized();
    const AxisView opposite = oppositeAxisView(m_state.axisView);
    if (opposite != AxisView::User) {
        target.axisView = opposite;
        target.viewQuat = axisViewQuat(opposite, m_state.axisRoll);
    } else {
        target.axisView = AxisView::User;
    }
    startSmoothView(target, nowSeconds, true);
}

void ViewNavigator::rollStep(Direction dir, double nowSeconds)
{
    // Blender: viewroll_exec(); rolls around the view axis.
    finishAnimation();
    float angle = m_prefs.numpadRotationStep;
    if (dir == Direction::Left) {
        angle = -angle;
    }
    const Vec3 mouseVec = -normalized(m_state.viewQuat.conjugated().rotate(Vec3(0, 0, 1)));
    ViewState target = m_state;
    target.viewQuat = (m_state.viewQuat * Quat::fromAxisAngle(mouseVec, angle)).normalized();
    target.axisView = AxisView::User;
    // Rolling an axis view by 90 degrees keeps it an axis view.
    AxisView view;
    int roll;
    if (m_state.axisView != AxisView::User &&
        quatToAxisView(target.viewQuat, 0.01f, view, roll)) {
        target.axisView = view;
        target.axisRoll = roll;
        target.viewQuat = axisViewQuat(view, roll);
    }
    startSmoothView(target, nowSeconds, true);
}

void ViewNavigator::panStep(Direction dir)
{
    // Blender: viewpan_invoke(), 32 px horizontally and 25 px vertically.
    finishAnimation();
    float x = 0.0f, y = 0.0f;
    switch (dir) {
    case Direction::Right: x = -kNumpadPanX; break;
    case Direction::Left: x = kNumpadPanX; break;
    case Direction::Up: y = -kNumpadPanY; break;
    case Direction::Down: y = kNumpadPanY; break;
    }
    // viewmove_apply(prev + (x, y)) => event_ofs = -(x, y) in Blender coordinates.
    m_state.pivot += winToDelta(Vec2(-x, -y), zfacAt(m_state.pivot));
}

void ViewNavigator::togglePerspective()
{
    // Blender: viewpersportho_exec(), immediate.
    finishAnimation();
    m_state.projection = m_state.projection == Projection::Orthographic ? Projection::Perspective
                                                                           : Projection::Orthographic;
}

void ViewNavigator::frameBounds(const Vec3 &min, const Vec3 &max, double nowSeconds)
{
    // Blender: view3d_from_minmax().
    finishAnimation();
    const Vec3 afm = max - min;
    const float size = std::max({afm.x, afm.y, afm.z});

    ViewState target = m_state;
    target.pivot = (min + max) * 0.5f;

    if (size > 0.0001f || m_state.projection == Projection::Orthographic) {
        const float radius = (size / 2.0f) * kViewMargin;
        // ED_view3d_radius_to_dist(): both projections reduce to radius / tan(half fov).
        float dist = radius / kTanHalfFov;
        // use_aspect
        float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
        if (aspect < 1.0f) {
            aspect = 1.0f / aspect;
        }
        dist *= aspect;
        if (m_state.projection == Projection::Perspective) {
            dist = std::max(dist, kClipStart * 1.5f);
        }
        target.dist = std::max(dist, distMin());
    }
    startSmoothView(target, nowSeconds, false);
}

void ViewNavigator::centerOn(const Vec3 &point, double nowSeconds)
{
    // Blender: viewcenter_pick_invoke() moves the view centre, keeping the distance.
    finishAnimation();
    ViewState target = m_state;
    target.pivot = point;
    startSmoothView(target, nowSeconds, false);
}

// ---------------------------------------------------------------------------
// Touch

void ViewNavigator::touchPanZoom(const Vec2 &prevCentroid, const Vec2 &centroid, float spanScale)
{
    finishAnimation();
    const float zfac = zfacAt(m_state.pivot);
    m_state.pivot += winToDelta(toBlender(prevCentroid) - toBlender(centroid), zfac);
    if (spanScale > 0.0f && spanScale != 1.0f) {
        float dfac = 1.0f / spanScale;
        dfac = std::clamp(dfac, distMin() / m_state.dist, distMax() / m_state.dist);
        zoomToWindow(dfac, &centroid);
    }
}

// ---------------------------------------------------------------------------
// Smooth view

void ViewNavigator::startSmoothView(const ViewState &target, double now, bool rotationOnly)
{
    // Blender: ED_view3d_smooth_view_ex().
    double duration = m_prefs.smoothViewMs / 1000.0;
    if (rotationOnly) {
        // Small rotations should not lag: scale by the angle (180 degrees == 1.0).
        duration *= angleBetween(target.viewQuat, m_state.viewQuat) / kPi;
    }
    if (duration <= 0.0) {
        m_state = target;
        m_anim.active = false;
        return;
    }
    m_anim.active = true;
    m_anim.src = m_state;
    m_anim.dst = target;
    m_anim.start = now;
    m_anim.duration = duration;
    m_state.projection = target.projection;
    m_state.axisView = target.axisView;
    m_state.axisRoll = target.axisRoll;
}

bool ViewNavigator::tick(double nowSeconds)
{
    if (!m_anim.active) {
        return false;
    }
    float factor = static_cast<float>((nowSeconds - m_anim.start) / m_anim.duration);
    if (factor >= 1.0f) {
        finishAnimation();
        return false;
    }
    factor = std::max(0.0f, factor);
    // Blender: view3d_smoothview_apply_with_interp() with a smoothstep curve.
    factor = 3.0f * factor * factor - 2.0f * factor * factor * factor;
    m_state.viewQuat = slerp(m_anim.src.viewQuat, m_anim.dst.viewQuat, factor).normalized();
    m_state.pivot = lerp(m_anim.src.pivot, m_anim.dst.pivot, factor);
    m_state.dist = m_anim.src.dist + (m_anim.dst.dist - m_anim.src.dist) * factor;
    return true;
}

void ViewNavigator::finishAnimation()
{
    if (m_anim.active) {
        m_state = m_anim.dst;
        m_anim.active = false;
    }
}

} // namespace atherta
