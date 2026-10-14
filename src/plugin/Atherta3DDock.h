/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "core/Mesh.h"
#include "core/MeshBvh.h"
#include "TextureSet.h"
#include "Viewport.h"

#include <KoCanvasObserverBase.h>

#include <QDockWidget>
#include <QPointer>
#include <QScopedPointer>
#include <QTimer>

class KisCanvas2;
class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class QToolButton;
class StrokeSession;

/**
 * "3D Paint" docker: loads a UV-unwrapped model, shows it with the texture
 * set of the active document and paints on it with Krita's active brush.
 */
class Atherta3DDock : public QDockWidget, public KoCanvasObserverBase, public ViewportPaintTarget
{
    Q_OBJECT
public:
    Atherta3DDock();
    ~Atherta3DDock() override;

    QString observerName() override { return QStringLiteral("Atherta3DDock"); }
    void setCanvas(KoCanvasBase *canvas) override;
    void unsetCanvas() override;

    // ViewportPaintTarget
    bool canPaint(QString *reason) const override;
    void beginStroke(KoPointerEvent *event, const QPointF &devicePos) override;
    void continueStroke(KoPointerEvent *event, const QPointF &devicePos) override;
    void endStroke() override;
    void tabletPointerChanged(const QTabletEvent *event) override;
    qreal brushDiameter() const override;

private Q_SLOTS:
    void slotLoadModel();
    void slotCreateTextureSet();
    void slotNewDocument(int size);
    void slotExportTextures();
    void slotImageUpdated(const QRect &rect);
    void slotFlushTextureUpdates();
    void slotLayerStackChanged();
    void slotImageSizeChanged();
    void slotStrokeUpdated();
    void slotGlReset();

private:
    void buildUi();
    void loadSettings();
    void saveSettings();
    void applySettingsToViewport();
    void updateState();
    void fullTextureSync();
    void syncRect(const QRect &rect);
    void applyBakedStroke(const std::vector<uint8_t> &premultiplied, const QRect &rect,
                          bool eraser, const QString &compositeOp);
    bool channelEnabled(atherta::Channel c) const;
    float channelValue(atherta::Channel c) const;

    QPointer<KisCanvas2> m_canvas;
    TextureSet m_textureSet;
    bool m_createTextureSetOnNextCanvas = false;

    atherta::Mesh m_mesh;
    atherta::MeshBvh m_bvh;
    QString m_modelPath;

    Viewport *m_viewport = nullptr;
    QScopedPointer<StrokeSession> m_stroke;

    QRect m_pendingDirty;
    QTimer m_syncTimer;
    QTimer m_refreshTimer;

    // UI
    QToolButton *m_textureSetButton = nullptr;
    QComboBox *m_viewCombo = nullptr;
    QCheckBox *m_paintColor = nullptr;
    QCheckBox *m_paintRoughness = nullptr;
    QCheckBox *m_paintMetallic = nullptr;
    QCheckBox *m_paintNormal = nullptr;
    QSlider *m_roughnessValue = nullptr;
    QSlider *m_metallicValue = nullptr;
    QLabel *m_status = nullptr;

    // Settings
    int m_padding = 8;
    float m_fadeStart = 0.2f;
};
