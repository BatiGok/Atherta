/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Viewport.h"

#include <KoPointerEvent.h>
#include <klocalizedstring.h>

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QWheelEvent>

#include <cmath>

using namespace atherta;

namespace {

// Blender: U.drag_threshold_mouse / U.drag_threshold_tablet.
constexpr qreal kDragThresholdMouse = 3.0;
// Touches closer than this to the last pen event are treated as palm contact.
constexpr qint64 kPalmRejectionMs = 500;

/**
 * Makes the viewport's context current and afterwards restores the previous
 * one. Krita's canvas uploads its textures assuming that a context of the
 * shared group is current (only macOS makes it current explicitly), so the
 * context must never be left released with doneCurrent().
 */
class ContextGuard
{
public:
    explicit ContextGuard(QOpenGLWidget *widget)
        : m_previous(QOpenGLContext::currentContext())
        , m_surface(m_previous ? m_previous->surface() : nullptr)
    {
        widget->makeCurrent();
    }
    ~ContextGuard()
    {
        if (m_previous && m_previous != QOpenGLContext::currentContext()) {
            m_previous->makeCurrent(m_surface);
        }
    }

private:
    QOpenGLContext *m_previous;
    QSurface *m_surface;
};

/** Numpad key with NumLock off arrives as navigation keys with KeypadModifier. */
int keypadDigit(int key)
{
    switch (key) {
    case Qt::Key_0: case Qt::Key_Insert: return 0;
    case Qt::Key_1: case Qt::Key_End: return 1;
    case Qt::Key_2: case Qt::Key_Down: return 2;
    case Qt::Key_3: case Qt::Key_PageDown: return 3;
    case Qt::Key_4: case Qt::Key_Left: return 4;
    case Qt::Key_5: case Qt::Key_Clear: return 5;
    case Qt::Key_6: case Qt::Key_Right: return 6;
    case Qt::Key_7: case Qt::Key_Home: return 7;
    case Qt::Key_8: case Qt::Key_Up: return 8;
    case Qt::Key_9: case Qt::Key_PageUp: return 9;
    default: return -1;
    }
}

QString viewName(const ViewState &s)
{
    QString name;
    switch (s.axisView) {
    case AxisView::Front: name = i18n("Front"); break;
    case AxisView::Back: name = i18n("Back"); break;
    case AxisView::Left: name = i18n("Left"); break;
    case AxisView::Right: name = i18n("Right"); break;
    case AxisView::Top: name = i18n("Top"); break;
    case AxisView::Bottom: name = i18n("Bottom"); break;
    case AxisView::User: name = i18n("User"); break;
    }
    return name + QLatin1Char(' ') +
           (s.projection == Projection::Perspective ? i18n("Perspective") : i18n("Orthographic"));
}

} // namespace

Viewport::Viewport(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_AcceptTouchEvents);
    setAttribute(Qt::WA_TabletTracking);
    setCursor(Qt::CrossCursor);
    setMinimumSize(200, 150);

    m_clock.start();
    m_animTimer.setInterval(16);
    connect(&m_animTimer, &QTimer::timeout, this, &Viewport::onAnimationTick);

    // Pen proximity is delivered to the application, not to the widget.
    qApp->installEventFilter(this);
}

Viewport::~Viewport()
{
    qApp->removeEventFilter(this);
    cleanupGL();
}

double Viewport::now() const
{
    return m_clock.nsecsElapsed() / 1e9;
}

QSize Viewport::deviceSize() const
{
    const qreal dpr = devicePixelRatioF();
    return QSize(qMax(1, qRound(width() * dpr)), qMax(1, qRound(height() * dpr)));
}

QPointF Viewport::toDevice(const QPointF &logical) const
{
    return logical * devicePixelRatioF();
}

void Viewport::setMesh(const Mesh *mesh, const MeshBvh *bvh)
{
    m_mesh = mesh;
    m_bvh = bvh;
    m_meshDirty = true;
    update();
}

void Viewport::frameAll(bool animate)
{
    if (!m_mesh || m_mesh->isEmpty()) {
        return;
    }
    m_nav.frameBounds(m_mesh->boundsMin, m_mesh->boundsMax, now());
    if (!animate) {
        m_nav.finishAnimation();
    }
    ensureAnimating();
    update();
}

void Viewport::setDisplayMode(Renderer::DisplayMode mode)
{
    if (m_drawParams.displayMode == mode) {
        return;
    }
    m_drawParams.displayMode = mode;
    Q_EMIT displayModeChanged(static_cast<int>(mode));
    update();
}

void Viewport::setMessage(const QString &message)
{
    m_message = message;
    update();
}

bool Viewport::withContext(const std::function<void(Renderer &, GLuint)> &fn)
{
    if (!m_glReady) {
        return false;
    }
    {
        ContextGuard guard(this);
        fn(m_renderer, defaultFramebufferObject());
    }
    update();
    return true;
}

// ---------------------------------------------------------------------------
// GL

void Viewport::initializeGL()
{
    // Re-parenting (floating the docker) destroys the context.
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &Viewport::cleanupGL,
            Qt::DirectConnection);
    QString error;
    if (!m_renderer.initialize(&error)) {
        m_glReady = false;
        Q_EMIT glError(error);
        return;
    }
    m_glReady = true;
    m_meshDirty = true;
    Q_EMIT glReset();
}

void Viewport::cleanupGL()
{
    if (!m_glReady) {
        return;
    }
    {
        ContextGuard guard(this);
        m_renderer.cleanup();
    }
    m_glReady = false;
}

void Viewport::resizeGL(int w, int h)
{
    m_nav.setViewportSize(w, h);
}

void Viewport::paintGL()
{
    m_nav.setViewportSize(width(), height());
    if (!m_glReady) {
        return;
    }
    if (m_meshDirty) {
        if (m_mesh) {
            m_renderer.setMesh(*m_mesh);
        } else {
            m_renderer.setMesh(Mesh());
        }
        m_meshDirty = false;
    }

    const ViewState &s = m_nav.state();
    Renderer::DrawParams &p = m_drawParams;
    p.viewProj = m_nav.viewProjectionMatrix();
    p.cameraPos = m_nav.eyePosition();
    p.viewDir = m_nav.viewDirection();
    p.viewQuat = s.viewQuat;
    p.ortho = s.projection == Projection::Orthographic;
    p.dist = s.dist;
    p.pivot = s.pivot;
    p.viewportDevice = deviceSize();
    p.showGrid = m_options.showGrid;
    m_renderer.draw(p, defaultFramebufferObject());

    drawOverlay();
}

void Viewport::drawOverlay()
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Blender-style view name in the top left corner.
    QFont font = painter.font();
    font.setPointSizeF(font.pointSizeF() * 0.95);
    painter.setFont(font);
    painter.setPen(QColor(230, 230, 230));
    painter.drawText(QRect(10, 6, width() - 20, 20), Qt::AlignLeft | Qt::AlignVCenter,
                     viewName(m_nav.state()));

    if (!m_message.isEmpty()) {
        painter.setPen(QColor(200, 200, 200));
        painter.drawText(rect().adjusted(20, 20, -20, -20), Qt::AlignCenter | Qt::TextWordWrap,
                         m_message);
    }

    // Brush outline.
    if (m_hovering && m_navOp == NavOp::None && m_paintTarget && m_mesh) {
        const qreal radius = m_paintTarget->brushDiameter() / devicePixelRatioF() * 0.5;
        if (radius > 0.5) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(0, 0, 0, 160), 2.0));
            painter.drawEllipse(m_hoverPos, radius, radius);
            painter.setPen(QPen(QColor(255, 255, 255, 220), 1.0));
            painter.drawEllipse(m_hoverPos, radius, radius);
        }
    }
}

void Viewport::ensureAnimating()
{
    const bool needed = m_nav.isAnimating() ||
                        (m_navOp == NavOp::Zoom && m_nav.zoomNeedsTimer());
    if (needed && !m_animTimer.isActive()) {
        m_animTimer.start();
    }
}

void Viewport::onAnimationTick()
{
    bool keep = m_nav.tick(now());
    if (m_navOp == NavOp::Zoom && m_nav.zoomNeedsTimer()) {
        m_nav.updateZoom(toVec(m_hoverPos), now());
        keep = true;
    }
    if (!keep) {
        m_animTimer.stop();
    }
    update();
}

// ---------------------------------------------------------------------------
// Navigation

bool Viewport::beginNavigation(Qt::KeyboardModifiers mods, const QPointF &pos, bool fromSpace)
{
    m_nav.finishAnimation();
    m_pressPos = pos;
    const Vec2 p = toVec(pos);
    const bool shift = mods & Qt::ShiftModifier;
    const bool ctrl = mods & Qt::ControlModifier;
    const bool alt = mods & Qt::AltModifier;
    m_navStartedWithAlt = alt;

    // Blender: MMB rotate, Shift+MMB pan, Ctrl+MMB zoom, Shift+Ctrl+MMB dolly,
    // Alt+MMB click centres on the point, Alt+MMB drag picks an axis view.
    if (alt && !fromSpace && !m_options.emulate3ButtonMouse) {
        m_navOp = NavOp::AltPending;
    } else if (shift && ctrl) {
        m_navOp = NavOp::Dolly;
        m_nav.beginDolly(p);
    } else if (shift) {
        m_navOp = NavOp::Pan;
        m_nav.beginPan(p);
    } else if (ctrl) {
        m_navOp = NavOp::Zoom;
        m_nav.beginZoom(p, now());
        ensureAnimating();
    } else {
        m_navOp = NavOp::Rotate;
        m_nav.beginRotate(p);
    }
    update();
    return true;
}

void Viewport::updateNavigation(const QPointF &pos, Qt::KeyboardModifiers mods)
{
    const Vec2 p = toVec(pos);
    switch (m_navOp) {
    case NavOp::Rotate:
        // Holding Alt while orbiting snaps to axis views (Blender). When Alt
        // started the navigation (emulated MMB) it is not a snap request.
        m_nav.updateRotate(p, (mods & Qt::AltModifier) && !m_navStartedWithAlt);
        break;
    case NavOp::Pan:
        m_nav.updatePan(p);
        break;
    case NavOp::Zoom:
        m_nav.updateZoom(p, now());
        break;
    case NavOp::Dolly:
        m_nav.updateDolly(p);
        break;
    case NavOp::AltPending:
        if (QLineF(pos, m_pressPos).length() > kDragThresholdMouse) {
            finishAltDrag(pos);
        }
        break;
    case NavOp::None:
        break;
    }
    update();
}

void Viewport::finishAltDrag(const QPointF &pos)
{
    // Blender: Alt+MMB click-drag towards a direction selects an axis view.
    const QPointF d = pos - m_pressPos;
    // Angle with Y pointing up, 0 = east, counter clockwise.
    const qreal angle = std::atan2(-d.y(), d.x()) * 180.0 / M_PI;
    const int sector = (qRound(angle / 45.0) + 8) % 8; // 0 E, 1 NE, 2 N, 3 NW, 4 W, 5 SW, 6 S, 7 SE
    AxisView view = AxisView::User;
    switch (sector) {
    case 0: view = AxisView::Right; break;
    case 1: view = AxisView::Back; break;
    case 2: view = AxisView::Top; break;
    case 3: view = AxisView::Front; break;
    case 4: view = AxisView::Left; break;
    case 6: view = AxisView::Bottom; break;
    default: break;
    }
    if (view != AxisView::User) {
        m_nav.setAxisView(view, now());
        ensureAnimating();
    }
    m_navOp = NavOp::None;
}

void Viewport::centerOnPoint(const QPointF &pos)
{
    if (!m_bvh || m_bvh->isEmpty()) {
        return;
    }
    const RayHit hit = m_bvh->intersect(m_nav.rayThroughPixel(toVec(pos)));
    if (hit.hit) {
        m_nav.centerOn(hit.position, now());
        ensureAnimating();
    }
}

void Viewport::endNavigation(const QPointF &pos)
{
    if (m_navOp == NavOp::AltPending) {
        centerOnPoint(pos);
    }
    m_nav.endDrag();
    m_navOp = NavOp::None;
    update();
}

// ---------------------------------------------------------------------------
// Painting

bool Viewport::beginPaint(KoPointerEvent *ev, const QPointF &pos)
{
    if (!m_paintTarget) {
        return false;
    }
    QString reason;
    if (!m_paintTarget->canPaint(&reason)) {
        setMessage(reason);
        return false;
    }
    m_nav.finishAnimation();
    m_painting = true;
    m_paintTarget->beginStroke(ev, toDevice(pos));
    return true;
}

void Viewport::endPaint()
{
    if (m_painting) {
        m_painting = false;
        m_touchPainting = false;
        if (m_paintTarget) {
            m_paintTarget->endStroke();
        }
    }
}

// ---------------------------------------------------------------------------
// Events

bool Viewport::event(QEvent *e)
{
    switch (e->type()) {
    case QEvent::ShortcutOverride: {
        // Take our navigation keys before Krita's shortcuts see them.
        auto *ke = static_cast<QKeyEvent *>(e);
        if (handleKey(ke, true) || ke->key() == Qt::Key_Space) {
            e->accept();
            return true;
        }
        break;
    }
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
        handleTouch(static_cast<QTouchEvent *>(e));
        return true;
    default:
        break;
    }
    return QOpenGLWidget::event(e);
}

bool Viewport::eventFilter(QObject *watched, QEvent *e)
{
    if (e->type() == QEvent::TabletEnterProximity) {
        m_stylusNear = true;
        if (m_paintTarget) {
            m_paintTarget->tabletPointerChanged(static_cast<QTabletEvent *>(e));
        }
    } else if (e->type() == QEvent::TabletLeaveProximity) {
        m_stylusNear = false;
    }
    return QOpenGLWidget::eventFilter(watched, e);
}

void Viewport::mousePressEvent(QMouseEvent *e)
{
    // Ignore mouse events synthesized from touch or pen input.
    if (e->source() != Qt::MouseEventNotSynthesized) {
        e->ignore();
        return;
    }
    setFocus(Qt::MouseFocusReason);
    if (m_painting || m_navOp != NavOp::None) {
        return;
    }
    const Qt::KeyboardModifiers mods = e->modifiers();
    const bool emulatedMMB = m_options.emulate3ButtonMouse && e->button() == Qt::LeftButton &&
                             (mods & Qt::AltModifier);

    if (e->button() == Qt::MiddleButton || emulatedMMB) {
        beginNavigation(mods, e->localPos(), false);
    } else if (e->button() == Qt::LeftButton && m_spaceHeld) {
        beginNavigation(mods & ~Qt::AltModifier, e->localPos(), true);
    } else if (e->button() == Qt::LeftButton) {
        KoPointerEvent ev(e, toDevice(e->localPos()));
        beginPaint(&ev, e->localPos());
    } else if (e->button() == Qt::BackButton) {
        // Blender: BUTTON4MOUSE frames the selection.
        frameAll(true);
    }
    e->accept();
}

void Viewport::mouseMoveEvent(QMouseEvent *e)
{
    if (e->source() != Qt::MouseEventNotSynthesized) {
        e->ignore();
        return;
    }
    m_hoverPos = e->localPos();
    m_hovering = true;
    if (m_painting) {
        KoPointerEvent ev(e, toDevice(e->localPos()));
        m_paintTarget->continueStroke(&ev, toDevice(e->localPos()));
    } else if (m_navOp != NavOp::None) {
        updateNavigation(e->localPos(), e->modifiers());
    }
    update();
    e->accept();
}

void Viewport::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->source() != Qt::MouseEventNotSynthesized) {
        e->ignore();
        return;
    }
    if (m_painting && e->button() == Qt::LeftButton) {
        endPaint();
    } else if (m_navOp != NavOp::None) {
        endNavigation(e->localPos());
    }
    e->accept();
}

void Viewport::wheelEvent(QWheelEvent *e)
{
    const QPointF pos = e->position();
    const Vec2 cursor = toVec(pos);
    const Qt::KeyboardModifiers mods = e->modifiers();

    if (!e->pixelDelta().isNull() && e->phase() != Qt::NoScrollPhase) {
        // Trackpad: Blender's TRACKPADPAN rotates, Shift pans, Ctrl zooms.
        const QPointF d = e->pixelDelta();
        if (mods & Qt::ControlModifier) {
            m_nav.beginZoom(cursor, now());
            m_nav.updateZoom(cursor + Vec2(0.0f, float(-d.y())), now());
        } else if (mods & Qt::ShiftModifier) {
            m_nav.beginPan(cursor);
            m_nav.updatePan(cursor + Vec2(float(d.x()), float(d.y())));
        } else {
            m_nav.beginRotate(cursor);
            m_nav.updateRotate(cursor + Vec2(float(-d.x()), float(-d.y())));
        }
        m_nav.endDrag();
    } else {
        m_wheelAccumulator += e->angleDelta().y();
        const int steps = m_wheelAccumulator / 120;
        m_wheelAccumulator -= steps * 120;
        if (steps != 0) {
            m_nav.zoomStep(steps, &cursor);
        }
    }
    update();
    e->accept();
}

void Viewport::tabletEvent(QTabletEvent *e)
{
    m_lastTabletEvent = m_clock.elapsed();
    const QPointF pos = e->posF();
    m_hoverPos = pos;
    m_hovering = true;

    switch (e->type()) {
    case QEvent::TabletPress:
        setFocus(Qt::MouseFocusReason);
        if (m_paintTarget) {
            m_paintTarget->tabletPointerChanged(e);
        }
        if (m_painting || m_navOp != NavOp::None) {
            break;
        }
        if (e->button() == Qt::LeftButton && !m_spaceHeld) {
            KoPointerEvent ev(e, toDevice(pos));
            beginPaint(&ev, pos);
        } else if (e->button() == Qt::LeftButton) {
            beginNavigation(e->modifiers() & ~Qt::AltModifier, pos, true);
        } else if (e->button() == Qt::MiddleButton) {
            // Pen side button mapped to the middle button: Blender navigation.
            beginNavigation(e->modifiers(), pos, false);
        }
        break;
    case QEvent::TabletMove:
        if (m_painting) {
            KoPointerEvent ev(e, toDevice(pos));
            m_paintTarget->continueStroke(&ev, toDevice(pos));
        } else if (m_navOp != NavOp::None) {
            updateNavigation(pos, e->modifiers());
        }
        break;
    case QEvent::TabletRelease:
        if (m_painting && e->button() == Qt::LeftButton) {
            endPaint();
        } else if (m_navOp != NavOp::None) {
            endNavigation(pos);
        }
        break;
    default:
        break;
    }
    update();
    e->accept();
}

void Viewport::handleTouch(QTouchEvent *e)
{
    e->accept();
    if (e->device() && e->device()->type() != QTouchDevice::TouchScreen) {
        return; // touchpads arrive as wheel events
    }

    const bool palm = m_stylusNear || (m_clock.elapsed() - m_lastTabletEvent) < kPalmRejectionMs;

    QPointF centroid;
    int count = 0;
    QList<QPointF> points;
    if (e->type() != QEvent::TouchEnd && e->type() != QEvent::TouchCancel) {
        for (const QTouchEvent::TouchPoint &tp : e->touchPoints()) {
            if (tp.state() != Qt::TouchPointReleased) {
                points.append(tp.pos());
                centroid += tp.pos();
                ++count;
            }
        }
    }
    if (palm || m_painting != m_touchPainting) {
        // A pen (or mouse) owns the input: drop any touch gesture.
        if (m_touchCount > 0 && !m_painting) {
            m_nav.endDrag();
        }
        m_touchCount = 0;
        return;
    }
    if (count > 0) {
        centroid /= count;
    }
    qreal span = 0.0;
    if (count >= 2) {
        span = QLineF(points[0], points[1]).length();
    }

    if (count != m_touchCount) {
        // Finger count changed: end the previous gesture, start a new one.
        if (m_touchPainting) {
            endPaint();
        }
        m_nav.endDrag();
        m_touchCount = count;
        m_touchCentroid = centroid;
        m_touchSpan = span;
        if (count == 1) {
            if (m_options.fingerPainting) {
                KoPointerEvent ev(e, toDevice(centroid));
                m_touchPainting = beginPaint(&ev, centroid);
            } else {
                m_nav.finishAnimation();
                m_nav.beginRotate(toVec(centroid));
            }
        }
        update();
        return;
    }

    if (count == 1) {
        if (m_touchPainting) {
            KoPointerEvent ev(e, toDevice(centroid));
            m_paintTarget->continueStroke(&ev, toDevice(centroid));
        } else {
            m_nav.updateRotate(toVec(centroid));
        }
    } else if (count == 2) {
        const float scale = m_touchSpan > 1.0 ? float(span / m_touchSpan) : 1.0f;
        m_nav.touchPanZoom(toVec(m_touchCentroid), toVec(centroid), scale);
    }
    m_touchCentroid = centroid;
    m_touchSpan = span;
    update();
}

bool Viewport::handleKey(QKeyEvent *e, bool dryRun)
{
    const Qt::KeyboardModifiers mods = e->modifiers();
    const bool keypad = mods & Qt::KeypadModifier;
    const bool ctrl = mods & Qt::ControlModifier;
    const bool shift = mods & Qt::ShiftModifier;
    const int key = e->key();
    const double t = now();

    std::function<void()> action;

    if (keypad) {
        switch (keypadDigit(key)) {
        case 1: action = [=] { m_nav.setAxisView(ctrl ? AxisView::Back : AxisView::Front, t); }; break;
        case 3: action = [=] { m_nav.setAxisView(ctrl ? AxisView::Left : AxisView::Right, t); }; break;
        case 7: action = [=] { m_nav.setAxisView(ctrl ? AxisView::Bottom : AxisView::Top, t); }; break;
        case 5: action = [=] { m_nav.togglePerspective(); }; break;
        case 9: action = [=] { m_nav.orbitOpposite(t); }; break;
        case 2: case 4: case 6: case 8: {
            const int d = keypadDigit(key);
            const Direction dir = d == 2 ? Direction::Down
                                : d == 4 ? Direction::Left
                                : d == 6 ? Direction::Right
                                         : Direction::Up;
            if (ctrl) {
                action = [=] { m_nav.panStep(dir); };
            } else if (shift && (d == 4 || d == 6)) {
                action = [=] { m_nav.rollStep(dir, t); };
            } else {
                action = [=] { m_nav.orbitStep(dir, t); };
            }
            break;
        }
        default:
            break;
        }
        if (!action) {
            if (key == Qt::Key_Plus) {
                action = [=] { shift ? m_nav.dollyStep(1) : m_nav.zoomStep(1); };
            } else if (key == Qt::Key_Minus) {
                action = [=] { shift ? m_nav.dollyStep(-1) : m_nav.zoomStep(-1); };
            } else if (key == Qt::Key_Period || key == Qt::Key_Delete || key == Qt::Key_Comma) {
                action = [=] { frameAll(true); };
            }
        }
    } else {
        if (key == Qt::Key_Home) {
            action = [=] { frameAll(true); };
        } else if (ctrl && (key == Qt::Key_Equal || key == Qt::Key_Plus)) {
            action = [=] { m_nav.zoomStep(1); };
        } else if (ctrl && key == Qt::Key_Minus) {
            action = [=] { m_nav.zoomStep(-1); };
        } else if (key == Qt::Key_C && shift && !ctrl) {
            action = [=] { frameAll(true); };
        } else if (key == Qt::Key_C && !shift && !ctrl && !(mods & Qt::AltModifier)) {
            // Cycle through the channel views, like Substance Painter.
            action = [=] {
                const int next = (static_cast<int>(m_drawParams.displayMode) + 1) % 5;
                setDisplayMode(static_cast<Renderer::DisplayMode>(next));
            };
        }
    }

    if (!action) {
        return false;
    }
    if (!dryRun) {
        action();
        ensureAnimating();
        update();
    }
    return true;
}

void Viewport::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Space) {
        if (!e->isAutoRepeat()) {
            m_spaceHeld = true;
        }
        e->accept();
        return;
    }
    if (handleKey(e, false)) {
        e->accept();
        return;
    }
    QOpenGLWidget::keyPressEvent(e);
}

void Viewport::keyReleaseEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        m_spaceHeld = false;
        e->accept();
        return;
    }
    QOpenGLWidget::keyReleaseEvent(e);
}

void Viewport::enterEvent(QEvent *e)
{
    // Like Blender, keys go to the view under the cursor.
    setFocus(Qt::MouseFocusReason);
    m_hovering = true;
    QOpenGLWidget::enterEvent(e);
}

void Viewport::leaveEvent(QEvent *e)
{
    m_hovering = false;
    update();
    QOpenGLWidget::leaveEvent(e);
}

void Viewport::focusOutEvent(QFocusEvent *e)
{
    m_spaceHeld = false;
    QOpenGLWidget::focusOutEvent(e);
}
