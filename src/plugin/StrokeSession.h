/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QImage>
#include <QObject>
#include <QRect>
#include <QScopedPointer>
#include <QSize>

#include <kis_image.h>
#include <kis_node.h>
#include <kis_types.h>

class KoCanvasResourceProvider;
class KoPointerEvent;
class KisPaintingInformationBuilder;
class KisToolFreehandHelper;

/**
 * Runs one brush stroke with Krita's *active* brush preset, but paints into a
 * hidden, transparent image that has the size of the 3D viewport. The 3D
 * view shows this screen-space layer live, and when the stroke ends it is
 * projected onto the mesh and baked into the texture layers.
 *
 * Krita's freehand helper does the real work, so pressure curves, smoothing,
 * stabilizer, airbrushing, spacing and every paint engine behave exactly as
 * on the 2D canvas.
 */
class StrokeSession : public QObject
{
    Q_OBJECT
public:
    /**
     * @param sourceResources the canvas resources of the active Krita view
     *        (brush preset, colours, opacity, ...). They are copied, so the
     *        stroke is not affected by later UI changes.
     * @param size viewport size in device pixels.
     */
    StrokeSession(KoCanvasResourceProvider *sourceResources, const QSize &size,
                  QObject *parent = nullptr);
    ~StrokeSession() override;

    /** True when Krita is in eraser mode (or the eraser tip is used). */
    bool isEraser() const { return m_eraser; }
    /** Blending mode of the brush; applied when the stroke is baked. */
    QString compositeOp() const { return m_compositeOp; }
    /** Brush opacity is applied by the paint engine inside the stroke image. */

    void begin(KoPointerEvent *event, const QPointF &pos);
    void move(KoPointerEvent *event);
    /** Ends the stroke and waits until Krita finished rendering it. */
    void end();
    bool isActive() const { return m_active; }

    QSize size() const { return m_size; }

    /** Premultiplied RGBA8888 copy of @p rect of the stroke image. */
    QImage read(const QRect &rect) const;

    /** Union of the areas changed since the last call. */
    QRect takeDirtyRect();
    /** Everything the stroke touched so far. */
    QRect totalDirtyRect() const { return m_totalDirty; }

Q_SIGNALS:
    void updated();

private Q_SLOTS:
    void slotImageUpdated(const QRect &rect);

private:
    QSize m_size;
    bool m_eraser = false;
    bool m_active = false;
    QString m_compositeOp;
    QRect m_dirty;
    QRect m_totalDirty;

    QScopedPointer<KoCanvasResourceProvider> m_resources;
    QScopedPointer<KisPaintingInformationBuilder> m_infoBuilder;
    QScopedPointer<KisToolFreehandHelper> m_helper;
    KisImageSP m_image;
    KisNodeSP m_layer;
};
