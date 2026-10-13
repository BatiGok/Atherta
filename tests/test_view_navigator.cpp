/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Checks that ViewNavigator reproduces Blender's navigation numbers.
 */

#include "doctest.h"

#include "ViewNavigator.h"

using namespace atherta;
using doctest::Approx;

namespace {

constexpr int kW = 800;
constexpr int kH = 600;

ViewNavigator makeNav(AxisView view = AxisView::Front, Projection proj = Projection::Perspective)
{
    ViewNavigator nav;
    nav.setViewportSize(kW, kH);
    ViewState s;
    s.viewQuat = axisViewQuat(view);
    s.axisView = view;
    s.projection = proj;
    s.dist = 10.0f;
    nav.setState(s);
    return nav;
}

void checkVec(const Vec3 &a, const Vec3 &b, double eps = 1e-4)
{
    CHECK(a.x == Approx(b.x).epsilon(eps).scale(1.0));
    CHECK(a.y == Approx(b.y).epsilon(eps).scale(1.0));
    CHECK(a.z == Approx(b.z).epsilon(eps).scale(1.0));
}

void checkPixel(const Vec2 &a, const Vec2 &b, double eps = 0.05)
{
    CHECK(a.x == Approx(b.x).epsilon(eps).scale(1.0));
    CHECK(a.y == Approx(b.y).epsilon(eps).scale(1.0));
}

/** Rotate the view's horizontal axis into world space. */
Vec3 viewRightInWorld(const ViewNavigator &nav)
{
    return nav.state().viewQuat.conjugated().rotate(Vec3(1, 0, 0));
}

} // namespace

TEST_CASE("Axis views look along Blender's axes")
{
    struct Expect {
        AxisView view;
        Vec3 dir;
    };
    const Expect expect[] = {
        {AxisView::Front, {0, 1, 0}},  {AxisView::Back, {0, -1, 0}},
        {AxisView::Right, {-1, 0, 0}}, {AxisView::Left, {1, 0, 0}},
        {AxisView::Top, {0, 0, -1}},   {AxisView::Bottom, {0, 0, 1}},
    };
    for (const auto &e : expect) {
        ViewNavigator nav = makeNav(e.view);
        checkVec(nav.viewDirection(), e.dir);
    }

    // Front view: X to the right, Z up on screen.
    ViewNavigator nav = makeNav(AxisView::Front);
    checkVec(viewRightInWorld(nav), Vec3(1, 0, 0));
    checkVec(nav.state().viewQuat.conjugated().rotate(Vec3(0, 1, 0)), Vec3(0, 0, 1));
}

TEST_CASE("Axis quaternion table matches the view basis construction")
{
    const Quat front = quatFromViewBasis(Vec3(1, 0, 0), Vec3(0, 0, 1), Vec3(0, -1, 0));
    CHECK(angleBetween(front, axisViewQuat(AxisView::Front)) < 1e-4f);

    for (int v = 1; v <= 6; ++v) {
        for (int r = 0; r < 4; ++r) {
            CHECK(axisViewQuat(static_cast<AxisView>(v), r).length() == Approx(1.0f));
        }
    }
}

TEST_CASE("Viewport field of view matches Blender's 50mm viewport lens")
{
    // Sensor 36mm, lens 50mm, viewport zoom 2 => tan(fov/2) = 0.72 on the long side.
    ViewNavigator nav = makeNav(AxisView::Front);
    const float halfWidthAtPivot = 0.72f * 10.0f;
    const Vec2 edge = nav.worldToPixel(Vec3(halfWidthAtPivot, 0, 0));
    CHECK(edge.x == Approx(float(kW)).epsilon(1e-3));
    CHECK(edge.y == Approx(kH / 2.0f).epsilon(1e-3));

    // Orthographic shows the same area at the pivot depth.
    nav.togglePerspective();
    CHECK(nav.state().projection == Projection::Orthographic);
    const Vec2 edgeOrtho = nav.worldToPixel(Vec3(halfWidthAtPivot, 0, 0));
    CHECK(edgeOrtho.x == Approx(float(kW)).epsilon(1e-3));
}

TEST_CASE("Turntable orbit: 0.4 degrees per pixel around world Z")
{
    ViewNavigator nav = makeNav(AxisView::Front);
    const Vec3 dirBefore = nav.viewDirection();

    nav.beginRotate(Vec2(400, 300));
    nav.updateRotate(Vec2(500, 300)); // 100 px to the right
    nav.endDrag();

    const Vec3 dirAfter = nav.viewDirection();
    CHECK(rad2deg(angleNormalized(dirBefore, dirAfter)) == Approx(40.0f).epsilon(1e-3));
    CHECK(dirAfter.z == Approx(0.0f).scale(1.0));
    // Dragging right orbits the eye towards -X, the model follows the cursor.
    CHECK(nav.eyePosition().x < 0.0f);
    CHECK(nav.state().axisView == AxisView::User);
}

TEST_CASE("Turntable orbit: dragging down looks from above")
{
    ViewNavigator nav = makeNav(AxisView::Front);
    nav.beginRotate(Vec2(400, 300));
    nav.updateRotate(Vec2(400, 400)); // 100 px down
    nav.endDrag();

    CHECK(nav.eyePosition().z > 0.0f);
    CHECK(rad2deg(std::asin(-nav.viewDirection().z)) == Approx(40.0f).epsilon(1e-3));
}

TEST_CASE("Turntable orbit keeps the horizon level")
{
    ViewNavigator nav = makeNav(AxisView::Front);
    nav.beginRotate(Vec2(100, 100));
    nav.updateRotate(Vec2(173, 141));
    nav.updateRotate(Vec2(250, 37));
    nav.updateRotate(Vec2(-120, 260));
    nav.endDrag();
    CHECK(viewRightInWorld(nav).z == Approx(0.0f).scale(1.0));
}

TEST_CASE("Auto perspective: axis views are orthographic, orbiting restores perspective")
{
    ViewNavigator nav = makeNav(AxisView::Front);
    nav.setAxisView(AxisView::Top, 0.0);
    CHECK(nav.state().projection == Projection::Orthographic);
    CHECK(nav.isAnimating());
    nav.tick(10.0);
    CHECK_FALSE(nav.isAnimating());
    CHECK(nav.state().axisView == AxisView::Top);
    checkVec(nav.viewDirection(), Vec3(0, 0, -1));

    nav.beginRotate(Vec2(10, 10));
    CHECK(nav.state().projection == Projection::Perspective);
    // Like Blender, the view only becomes a user view once the cursor moves.
    CHECK(nav.state().axisView == AxisView::Top);
    nav.updateRotate(Vec2(20, 10));
    CHECK(nav.state().axisView == AxisView::User);
    nav.endDrag();

    // A user toggled orthographic view stays orthographic when orbiting.
    nav.togglePerspective();
    nav.beginRotate(Vec2(10, 10));
    nav.updateRotate(Vec2(30, 10));
    CHECK(nav.state().projection == Projection::Orthographic);
}

TEST_CASE("Alt while orbiting snaps to an axis view")
{
    ViewNavigator nav = makeNav(AxisView::Front);
    nav.beginRotate(Vec2(400, 300));
    nav.updateRotate(Vec2(420, 290)); // 8 and 4 degrees away from front
    CHECK(nav.state().axisView == AxisView::User);
    nav.updateRotate(Vec2(420, 290), true);
    CHECK(nav.state().axisView == AxisView::Front);
    CHECK(nav.state().projection == Projection::Orthographic);
    checkVec(nav.viewDirection(), Vec3(0, 1, 0));

    // Releasing Alt restores the projection.
    nav.updateRotate(Vec2(500, 290), false);
    CHECK(nav.state().projection == Projection::Perspective);
}

TEST_CASE("Wheel zoom uses a factor of 1.2 per step")
{
    ViewNavigator nav = makeNav();
    nav.zoomStep(1);
    CHECK(nav.state().dist == Approx(10.0f / 1.2f));
    nav.zoomStep(-2);
    CHECK(nav.state().dist == Approx(10.0f * 1.2f));
    checkVec(nav.state().pivot, Vec3(0, 0, 0));
}

TEST_CASE("Zoom to mouse position keeps the point under the cursor")
{
    for (Projection proj : {Projection::Perspective, Projection::Orthographic}) {
        ViewNavigator nav = makeNav(AxisView::Front, proj);
        nav.preferences().zoomToMousePosition = true;
        const Vec2 cursor(620, 120);
        // A world point on the pivot plane under the cursor.
        const Ray ray = nav.rayThroughPixel(cursor);
        const float t = -ray.origin.y / ray.direction.y;
        const Vec3 world = ray.origin + ray.direction * t;

        nav.zoomStep(3, &cursor);
        checkPixel(nav.worldToPixel(world), cursor);
        CHECK(nav.state().dist == Approx(10.0f / (1.2f * 1.2f * 1.2f)));
    }
}

TEST_CASE("Pan: the pivot plane follows the cursor exactly")
{
    for (Projection proj : {Projection::Perspective, Projection::Orthographic}) {
        ViewNavigator nav = makeNav(AxisView::Front, proj);
        const Vec3 world(1.5f, 0.0f, -2.0f);
        const Vec2 before = nav.worldToPixel(world);

        nav.beginPan(Vec2(300, 300));
        nav.updatePan(Vec2(330, 280));
        nav.updatePan(Vec2(350, 320));
        nav.endDrag();

        checkPixel(nav.worldToPixel(world), before + Vec2(50, 20));
    }
}

TEST_CASE("Ctrl+MMB dolly-style zoom")
{
    ViewNavigator nav = makeNav();
    nav.beginZoom(Vec2(400, 300));
    nav.updateZoom(Vec2(400, 200)); // up => zoom in
    const float expected = 10.0f * (2.0f * ((205.0f / 305.0f) - 1.0f) + 1.0f);
    CHECK(nav.state().dist == Approx(expected));
    nav.updateZoom(Vec2(400, 300)); // back to the start restores the distance
    CHECK(nav.state().dist == Approx(10.0f));
    nav.updateZoom(Vec2(400, 450)); // down => zoom out
    CHECK(nav.state().dist > 10.0f);
    nav.endDrag();
}

TEST_CASE("Shift+Ctrl+MMB dolly moves the pivot along the view")
{
    ViewNavigator nav = makeNav(AxisView::Front);
    nav.beginDolly(Vec2(400, 300));
    nav.updateDolly(Vec2(400, 290)); // 10 px up => forward by 10 * 0.01 * dist
    nav.endDrag();
    checkVec(nav.state().pivot, Vec3(0, 1.0f, 0));
    CHECK(nav.state().dist == Approx(10.0f));
}

TEST_CASE("Trackball orbit returns to the start when the cursor does")
{
    ViewNavigator nav = makeNav(AxisView::Front);
    nav.preferences().orbitMethod = OrbitMethod::Trackball;
    const Quat start = nav.state().viewQuat;
    nav.beginRotate(Vec2(400, 300));
    nav.updateRotate(Vec2(470, 250));
    CHECK(angleBetween(start, nav.state().viewQuat) > 0.1f);
    nav.updateRotate(Vec2(400, 300));
    CHECK(angleBetween(start, nav.state().viewQuat) < 1e-4f);
}

TEST_CASE("Numpad orbit, opposite view, roll and pan steps")
{
    ViewNavigator nav = makeNav(AxisView::Front);
    nav.preferences().smoothViewMs = 0;

    nav.orbitStep(Direction::Left, 0.0);
    CHECK(rad2deg(angleNormalized(nav.viewDirection(), Vec3(0, 1, 0))) == Approx(15.0f));
    CHECK(nav.state().axisView == AxisView::User);

    nav = makeNav(AxisView::Front);
    nav.preferences().smoothViewMs = 0;
    nav.orbitOpposite(0.0);
    CHECK(nav.state().axisView == AxisView::Back);
    checkVec(nav.viewDirection(), Vec3(0, -1, 0));

    nav = makeNav(AxisView::Front);
    nav.preferences().smoothViewMs = 0;
    nav.rollStep(Direction::Right, 0.0);
    CHECK(rad2deg(angleNormalized(viewRightInWorld(nav), Vec3(1, 0, 0))) == Approx(15.0f));
    checkVec(nav.viewDirection(), Vec3(0, 1, 0));

    nav = makeNav(AxisView::Front);
    const Vec2 before = nav.worldToPixel(Vec3(0, 0, 0));
    nav.panStep(Direction::Right);
    checkPixel(nav.worldToPixel(Vec3(0, 0, 0)), before - Vec2(32, 0));
    nav.panStep(Direction::Up);
    checkPixel(nav.worldToPixel(Vec3(0, 0, 0)), before - Vec2(32, -25));
}

TEST_CASE("Smooth view lasts 200 ms, rotation-only scales with the angle")
{
    ViewNavigator nav = makeNav(AxisView::Front);
    nav.setAxisView(AxisView::Right, 1.0); // 90 degrees => 100 ms
    CHECK(nav.tick(1.05));
    CHECK_FALSE(nav.tick(1.101));
    CHECK(nav.state().axisView == AxisView::Right);

    nav.frameBounds(Vec3(-1, -1, -1), Vec3(1, 1, 1), 2.0); // not rotation-only => 200 ms
    CHECK(nav.tick(2.19));
    CHECK_FALSE(nav.tick(2.201));
}

TEST_CASE("Frame bounds uses Blender's view3d_from_minmax distance")
{
    ViewNavigator nav = makeNav(AxisView::Front);
    nav.frameBounds(Vec3(1, 1, 1), Vec3(3, 3, 3), 0.0);
    nav.finishAnimation();
    checkVec(nav.state().pivot, Vec3(2, 2, 2));
    // size 2 => radius 1.4, / tan(half fov) 0.72, * aspect 800/600
    CHECK(nav.state().dist == Approx(1.4f / 0.72f * (800.0f / 600.0f)));
}

TEST_CASE("Ray through the viewport centre hits the pivot")
{
    ViewNavigator nav = makeNav(AxisView::Front);
    const Ray ray = nav.rayThroughPixel(Vec2(kW / 2.0f, kH / 2.0f));
    checkVec(ray.direction, Vec3(0, 1, 0));
    const float t = -ray.origin.y / ray.direction.y;
    checkVec(ray.origin + ray.direction * t, Vec3(0, 0, 0));
}

TEST_CASE("Two finger touch pans and pinch-zooms around the fingers")
{
    ViewNavigator nav = makeNav(AxisView::Front);
    const Vec3 world(1.0f, 0.0f, 1.0f);
    const Vec2 p = nav.worldToPixel(world);
    nav.touchPanZoom(p, p + Vec2(40, 10), 2.0f);
    CHECK(nav.state().dist == Approx(5.0f));
    checkPixel(nav.worldToPixel(world), p + Vec2(40, 10));
}
