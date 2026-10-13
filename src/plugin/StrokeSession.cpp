/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "StrokeSession.h"

#include <KoCanvasResourceProvider.h>
#include <KoCanvasResourcesIds.h>
#include <KoColorSpaceRegistry.h>
#include <KoCompositeOpRegistry.h>
#include <KoPointerEvent.h>
#include <kis_image.h>
#include <kis_paint_device.h>
#include <kis_paint_layer.h>
#include <kis_painting_information_builder.h>
#include <kis_tool_freehand_helper.h>
#include <kis_undo_stores.h>
#include <kundo2magicstring.h>

StrokeSession::StrokeSession(KoCanvasResourceProvider *source, const QSize &size, QObject *parent)
    : QObject(parent)
    , m_size(size)
    , m_resources(new KoCanvasResourceProvider())
{
    // Copy every canvas resource (preset, colours, pattern, gradient, opacity,
    // flow, size, ...). Derived resources are resolved by the source provider.
    auto copyRange = [&](int from, int to) {
        for (int key = from; key <= to; ++key) {
            const QVariant value = source->resource(key);
            if (value.isValid()) {
                m_resources->setResource(key, value);
            }
        }
    };
    copyRange(KoCanvasResource::ForegroundColor, KoCanvasResource::ApplicationSpeciality);
    copyRange(KoCanvasResource::HdrExposure, KoCanvasResource::BrushRotation);

    const QString effectiveOp =
        source->resource(KoCanvasResource::CurrentEffectiveCompositeOp).toString();
    m_eraser = effectiveOp == COMPOSITE_ERASE ||
               source->resource(KoCanvasResource::EraserMode).toBool();
    m_compositeOp = (m_eraser || effectiveOp.isEmpty()) ? COMPOSITE_OVER : effectiveOp;

    // The screen-space layer always receives plain "normal" paint. Erasing and
    // the brush blending mode are applied when the stroke is baked onto the
    // texture layers.
    m_resources->setResource(KoCanvasResource::CurrentCompositeOp, COMPOSITE_OVER);
    m_resources->setResource(KoCanvasResource::CurrentEffectiveCompositeOp, COMPOSITE_OVER);
    m_resources->setResource(KoCanvasResource::EraserMode, false);
    // 2D canvas mirroring makes no sense on a 3D surface.
    m_resources->setResource(KoCanvasResource::MirrorHorizontal, false);
    m_resources->setResource(KoCanvasResource::MirrorVertical, false);
    // No level-of-detail painting on the hidden image.
    m_resources->setResource(KoCanvasResource::LodAvailability, false);
    m_resources->setResource(KoCanvasResource::EffectiveLodAvailability, false);
    // One stroke-image pixel is one screen pixel.
    m_resources->setResource(KoCanvasResource::EffectiveZoom, 1.0);
    m_resources->setResource(KoCanvasResource::EffectivePhysicalZoom, 1.0);
    m_resources->setResource(KoCanvasResource::GlobalAlphaLock, false);
    m_resources->clearResource(KoCanvasResource::CurrentKritaNode);

    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    m_image = new KisImage(new KisDumbUndoStore(), size.width(), size.height(), cs,
                           QStringLiteral("atherta-3d-stroke"));
    KisPaintLayerSP layer = new KisPaintLayer(m_image, QStringLiteral("stroke"), OPACITY_OPAQUE_U8, cs);
    m_image->addNode(layer, m_image->root());
    m_layer = layer;
    m_image->initialRefreshGraph();

    // Emitted from Krita's worker threads; queued to the GUI thread.
    connect(m_image.data(), &KisImage::sigImageUpdated, this, &StrokeSession::slotImageUpdated,
            Qt::QueuedConnection);

    m_infoBuilder.reset(new KisPaintingInformationBuilder());
    // Without explicit smoothing options the helper reads the user's saved
    // freehand brush smoothing settings.
    m_helper.reset(new KisToolFreehandHelper(m_infoBuilder.data(), m_resources.data(),
                                             kundo2_noi18n("3D paint stroke")));
}

StrokeSession::~StrokeSession()
{
    if (m_active) {
        end();
    }
    m_helper.reset();
    if (m_image) {
        m_image->waitForDone();
        m_image->notifyAboutToBeDeleted();
    }
}

void StrokeSession::begin(KoPointerEvent *event, const QPointF &pos)
{
    m_active = true;
    m_helper->initPaint(event, pos, m_image, m_layer, m_image.data());
}

void StrokeSession::move(KoPointerEvent *event)
{
    if (m_active) {
        m_helper->paintEvent(event);
    }
}

void StrokeSession::end()
{
    if (!m_active) {
        return;
    }
    m_helper->endPaint();
    m_image->waitForDone();
    m_active = false;
    m_totalDirty |= m_image->projection()->exactBounds();
}

QImage StrokeSession::read(const QRect &rect) const
{
    const QRect r = rect & QRect(QPoint(0, 0), m_size);
    if (r.isEmpty()) {
        return QImage();
    }
    const KoColorProfile *srgb = KoColorSpaceRegistry::instance()->rgb8()->profile();
    return m_image->projection()->convertToQImage(srgb, r).convertToFormat(
        QImage::Format_RGBA8888_Premultiplied);
}

QRect StrokeSession::takeDirtyRect()
{
    const QRect r = m_dirty;
    m_dirty = QRect();
    return r;
}

void StrokeSession::slotImageUpdated(const QRect &rect)
{
    m_dirty |= rect;
    m_totalDirty |= rect;
    Q_EMIT updated();
}
