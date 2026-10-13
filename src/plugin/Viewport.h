/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "MeshBvh.h"
#include "Renderer.h"
#include "ViewNavigator.h"

#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QPointer>
#include <QTimer>

#include <functional>

class KoPointerEvent;
class QTabletEvent;
class QTouchEvent;

/** Receives the painting input of the viewport (implemented by the docker). */
class ViewportPaintTarget
{
public:
    virtual ~ViewportPaintTarget() = default;
    /** Returns false and a user visible reason when painting is not possible. */
    virtual bool canPaint(QString *reason) const = 0;
    virtual void beginStroke(KoPointerEvent *event, const QPointF &devicePos) = 0;
    virtual void continueStroke(KoPointerEvent *event, const QPointF &devicePos) = 0;
    virtual void endStroke() = 0;
    virtual void tabletPointerChanged(const QTabletEvent *event) = 0;
    /** Brush diameter in device pixels, for the cursor outline. */
    virtual qreal brushDiameter() const = 0;
};

struct ViewportOptions {
    /** Blender "Emulate 3 Button Mouse": Alt + LMB acts as MMB. */
    bool emulate3ButtonMouse = false;
    /** Touch input paints with one finger instead of orbiting. */
    bool fingerPainting = false;
    bool showGrid = true;
};

/**
 * The 3D viewport. Navigation follows Blender's default key map (see
 * atherta::ViewNavigator); the left mouse button / pen tip paints with the
 * active Krita brush. Fingers navigate: one finger orbits, two fingers pan
 * and pinch-zoom. Touches are ignored while a pen is near the screen.
 */
class Viewport : public QOpenGLWidget
{
    Q_OBJECT
public:
    explicit Viewport(QWidget *parent = nullptr);
    ~Viewport() override;

    atherta::ViewNavigator &navigator() { return m_nav; }
    ViewportOptions &options() { return m_options; }
    Renderer::DrawParams &drawParams() { return m_drawParams; }

    void setPaintTarget(ViewportPaintTarget *target) { m_paintTarget = target; }

    /** The mesh must stay alive while it is set. Pass nullptr to clear. */
    void setMesh(const atherta::Mesh *mesh, const atherta::MeshBvh *bvh);
    void frameAll(bool animate);

    void setDisplayMode(Renderer::DisplayMode mode);
    Renderer::DisplayMode displayMode() const { return m_drawParams.displayMode; }

    void setMessage(const QString &message);

    /** Viewport size in device pixels (the framebuffer size). */
    QSize deviceSize() const;

    bool isGLReady() const { return m_glReady; }

    /**
     * Runs @p fn with the GL context current. The second argument is the
     * widget's framebuffer, to be restored after offscreen rendering.
     */
    bool withContext(const std::function<void(Renderer &, GLuint)> &fn);

Q_SIGNALS:
    /** The GL context was (re)created; textures must be uploaded again. */
    void glReset();
    void displayModeChanged(int mode);
    void glError(const QString &message);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    bool event(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void tabletEvent(QTabletEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private Q_SLOTS:
    void cleanupGL();
    void onAnimationTick();

private:
    enum class NavOp { None, Rotate, Pan, Zoom, Dolly, AltPending };

    double now() const;
    QPointF toDevice(const QPointF &logical) const;
    atherta::Vec2 toVec(const QPointF &p) const { return {float(p.x()), float(p.y())}; }

    bool beginNavigation(Qt::KeyboardModifiers mods, const QPointF &pos, bool fromSpace);
    void updateNavigation(const QPointF &pos, Qt::KeyboardModifiers mods);
    void endNavigation(const QPointF &pos);
    void finishAltDrag(const QPointF &pos);
    void centerOnPoint(const QPointF &pos);

    bool beginPaint(KoPointerEvent *ev, const QPointF &pos);
    void endPaint();

    void handleTouch(QTouchEvent *event);
    bool handleKey(QKeyEvent *event, bool dryRun);
    void ensureAnimating();
    void drawOverlay();

    atherta::ViewNavigator m_nav;
    Renderer m_renderer;
    Renderer::DrawParams m_drawParams;
    ViewportOptions m_options;
    ViewportPaintTarget *m_paintTarget = nullptr;
    const atherta::Mesh *m_mesh = nullptr;
    const atherta::MeshBvh *m_bvh = nullptr;
    bool m_meshDirty = false;
    bool m_glReady = false;
    QString m_message;

    QElapsedTimer m_clock;
    QTimer m_animTimer;

    NavOp m_navOp = NavOp::None;
    bool m_navStartedWithAlt = false;
    QPointF m_pressPos;
    bool m_spaceHeld = false;

    bool m_painting = false;
    QPointF m_hoverPos;
    bool m_hovering = false;

    bool m_stylusNear = false;
    qint64 m_lastTabletEvent = -1000000;
    int m_touchCount = 0;
    QPointF m_touchCentroid;
    qreal m_touchSpan = 0.0;
    bool m_touchPainting = false;

    int m_wheelAccumulator = 0;
};
