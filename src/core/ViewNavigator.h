/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Blender-style 3D viewport navigation.
 *
 * The behaviour, speeds and defaults are taken from Blender's sources
 * (source/blender/editors/space_view3d/view3d_navigate*.cc and the default
 * user preferences in DNA_userdef_types.h) so that navigating feels the same
 * as in Blender's 3D viewport:
 *
 *   MMB drag            orbit (turntable, 0.4 deg/px; trackball optional)
 *   Shift+MMB drag      pan (the point under the pivot follows the cursor)
 *   Ctrl+MMB drag       zoom (dolly style)
 *   Shift+Ctrl+MMB      dolly (move the pivot along the view direction)
 *   Wheel               zoom in/out by a factor of 1.2 per notch
 *   Alt while orbiting  snap to the nearest axis view
 *   Numpad 1/3/7        front/right/top (Ctrl: back/left/bottom)
 *   Numpad 2/4/6/8      orbit by 15 deg; Ctrl: pan; Shift+4/6: roll
 *   Numpad 5            perspective / orthographic
 *   Numpad 9            opposite side
 *   Numpad +/-          zoom; Home / Numpad . frame the model
 *
 * Coordinates passed in are viewport-local logical pixels with the origin at
 * the top-left corner and Y pointing down (Qt convention). Blender uses Y up;
 * the conversion happens inside this class.
 *
 * The class is free of Qt and OpenGL so it can be unit tested.
 */

#pragma once

#include "Math.h"

namespace atherta {

enum class Projection { Perspective, Orthographic };

enum class AxisView { User, Front, Back, Left, Right, Top, Bottom };

enum class OrbitMethod { Turntable, Trackball };

/** Blender "Zoom Method" preference (USER_ZOOM_CONTINUE / SCALE / DOLLY). */
enum class ZoomStyle { Continue, Dolly, Scale };

enum class Direction { Left, Right, Up, Down };

/** Mirrors Blender's navigation preferences, with Blender's defaults. */
struct NavigationPreferences {
    OrbitMethod orbitMethod = OrbitMethod::Turntable;
    /** Radians per pixel (U.view_rotate_sensitivity_turntable). */
    float turntableSensitivity = deg2rad(0.4f);
    /** U.view_rotate_sensitivity_trackball. */
    float trackballSensitivity = 1.0f;
    /** USER_AUTOPERSP: axis views switch to orthographic and back. */
    bool autoPerspective = true;
    /** USER_ZOOM_TO_MOUSEPOS. */
    bool zoomToMousePosition = false;
    /** USER_ZOOM_INVERT (affects drag zoom, as in Blender). */
    bool invertZoomDirection = false;
    /** USER_ZOOM_HORIZ: drag zoom uses horizontal motion. */
    bool horizontalZoom = false;
    ZoomStyle zoomStyle = ZoomStyle::Dolly;
    /** U.pad_rot_angle: numpad orbit / roll step. */
    float numpadRotationStep = deg2rad(15.0f);
    /** U.smooth_viewtx in milliseconds, 0 disables animation. */
    int smoothViewMs = 200;
    /** UI_SCALE_FAC. Qt already gives logical pixels so 1.0 matches Blender at 100%. */
    float uiScale = 1.0f;
};

struct ViewState {
    /** World to view rotation (Blender rv3d->viewquat). */
    Quat viewQuat;
    /** Orbit centre. Blender stores its negation in rv3d->ofs. */
    Vec3 pivot;
    /** Distance from the pivot to the eye (rv3d->dist). */
    float dist = 10.0f;
    Projection projection = Projection::Perspective;
    AxisView axisView = AxisView::User;
    int axisRoll = 0;
};

class ViewNavigator
{
public:
    // Blender View3D / camera defaults.
    static constexpr float kLens = 50.0f;              // View3D.lens
    static constexpr float kSensorWidth = 36.0f;       // DEFAULT_SENSOR_WIDTH
    static constexpr float kViewportZoom = 2.0f;       // CAMERA_PARAM_ZOOM_INIT_PERSP
    static constexpr float kClipStart = 0.01f;         // View3D.clip_start
    static constexpr float kClipEnd = 1000.0f;         // View3D.clip_end
    static constexpr float kGrid = 1.0f;               // View3D.grid
    static constexpr float kViewMargin = 1.4f;         // VIEW3D_MARGIN
    static constexpr float kTrackballSize = 1.1f;      // V3D_OP_TRACKBALLSIZE
    static constexpr float kWheelZoomStep = 1.2f;      // view_zoom_apply_step
    static constexpr float kNumpadPanX = 32.0f;        // view3d.view_pan
    static constexpr float kNumpadPanY = 25.0f;

    ViewNavigator();

    NavigationPreferences &preferences() { return m_prefs; }
    const NavigationPreferences &preferences() const { return m_prefs; }

    const ViewState &state() const { return m_state; }
    void setState(const ViewState &state);

    void setViewportSize(int width, int height);
    int width() const { return m_width; }
    int height() const { return m_height; }

    // --- Matrices and projection helpers -------------------------------

    Mat4 viewMatrix() const;
    Mat4 projectionMatrix() const;
    Mat4 viewProjectionMatrix() const { return projectionMatrix() * viewMatrix(); }
    float nearClip() const;
    float farClip() const;

    /** Eye position for perspective; for orthographic, a point behind the view. */
    Vec3 eyePosition() const;
    /** Unit vector pointing into the screen. */
    Vec3 viewDirection() const;
    /** World-space ray through a viewport pixel (Y down). */
    Ray rayThroughPixel(const Vec2 &pixel) const;
    /** Project a world-space point to viewport pixels (Y down). */
    Vec2 worldToPixel(const Vec3 &world) const;

    // --- Mouse drag operations -----------------------------------------

    void beginRotate(const Vec2 &pos);
    /** @p snapToAxis is Blender's "Alt while orbiting" axis snapping. */
    void updateRotate(const Vec2 &pos, bool snapToAxis = false);

    void beginPan(const Vec2 &pos);
    void updatePan(const Vec2 &pos);

    void beginZoom(const Vec2 &pos, double timeSeconds = 0.0);
    void updateZoom(const Vec2 &pos, double timeSeconds = 0.0);
    /** Continue-style zoom keeps zooming while the button is held. */
    bool zoomNeedsTimer() const { return m_prefs.zoomStyle == ZoomStyle::Continue; }

    void beginDolly(const Vec2 &pos);
    void updateDolly(const Vec2 &pos);

    void endDrag();
    bool isDragging() const { return m_drag != Drag::None; }

    // --- Discrete operations -------------------------------------------

    /** Wheel zoom. Positive steps zoom in (WHEELINMOUSE / Numpad +). */
    void zoomStep(int steps, const Vec2 *cursor = nullptr);
    /** Keyboard dolly (Shift+Numpad +/-). */
    void dollyStep(int steps);

    void setAxisView(AxisView view, double nowSeconds);
    void orbitStep(Direction dir, double nowSeconds);
    /** Numpad 9: orbit 180 degrees (switches to the opposite axis view). */
    void orbitOpposite(double nowSeconds);
    void rollStep(Direction dir, double nowSeconds);
    void panStep(Direction dir);
    void togglePerspective();
    /** Home / Numpad period: frame an axis aligned bounding box. */
    void frameBounds(const Vec3 &min, const Vec3 &max, double nowSeconds);

    // --- Touch helpers ---------------------------------------------------

    /**
     * Two finger gesture: @p centroid moved from @p prevCentroid and the
     * finger span changed by @p spanScale (new / old). Pans so the content
     * follows the fingers and zooms around the centroid.
     */
    void touchPanZoom(const Vec2 &prevCentroid, const Vec2 &centroid, float spanScale);

    // --- Smooth view animation -----------------------------------------

    bool isAnimating() const { return m_anim.active; }
    /** Advances the smooth-view animation. Returns true while animating. */
    bool tick(double nowSeconds);
    void finishAnimation();

private:
    enum class Drag { None, Rotate, Pan, Zoom, Dolly };

    Vec2 toBlender(const Vec2 &qtPos) const;
    float zfacAt(const Vec3 &world) const;
    Vec3 winToDelta(const Vec2 &blenderDelta, float zfac) const;
    void zoomToWindow(float dfac, const Vec2 *qtPos);
    float distMin() const;
    float distMax() const;
    void ensurePerspectiveForRotate();
    void applyRotateSnap();
    Vec3 trackballVector(const Vec2 &qtPos) const;
    float zoomScaleValue(const Vec2 &curr, double now);
    void startSmoothView(const ViewState &target, double now, bool rotationOnly);

    NavigationPreferences m_prefs;
    ViewState m_state;
    int m_width = 1;
    int m_height = 1;

    struct DragData {
        Vec2 initPos;
        Vec2 prevPos;
        Quat initQuat;
        Vec3 initPivot;
        float initDist = 0.0f;
        float initZfac = 1.0f;
        float reverse = 1.0f;
        Vec3 trackVec;
        Vec3 dollyDir;
        Projection perspAfterEnsure = Projection::Perspective;
        double lastTime = 0.0;
    };
    Drag m_drag = Drag::None;
    DragData m_d;

    struct Animation {
        bool active = false;
        ViewState src;
        ViewState dst;
        double start = 0.0;
        double duration = 0.0;
    };
    Animation m_anim;
};

/** Blender's axis view orientation (ED_view3d_quat_from_axis_view). */
Quat axisViewQuat(AxisView view, int roll = 0);

/** Rotation whose rows are the view-space basis vectors expressed in world space. */
Quat quatFromViewBasis(const Vec3 &x, const Vec3 &y, const Vec3 &z);

} // namespace atherta
