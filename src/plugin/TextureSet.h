/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "Channels.h"

#include <QImage>
#include <QRect>
#include <QString>

#include <kis_image.h>
#include <kis_node.h>
#include <kis_types.h>

class KisViewManager;

/**
 * The PBR texture set stored in a Krita document:
 *
 *   Atherta 3D            (group)
 *     Base Color          (group)  <- channel group, read for the 3D preview
 *       Base Color Paint  (paint layer, default paint target)
 *       Base Color Base   (paint layer filled with the channel default)
 *     Roughness, Metallic, Normal ...
 *
 * The document size is the texture resolution. Image row 0 is the top of the
 * texture (V = 1), as in every image editor.
 */
class TextureSet
{
public:
    static QString rootGroupName();
    static QString channelGroupName(atherta::Channel c);

    TextureSet() = default;
    explicit TextureSet(KisImageSP image);

    void setImage(KisImageSP image);
    KisImageSP image() const { return m_image; }

    /** Re-finds the layers by name; call after the layer stack changed. */
    void refresh();

    bool isValid() const;
    QSize size() const;

    KisNodeSP root() const { return m_root; }
    KisNodeSP channelGroup(atherta::Channel c) const;

    /**
     * Layer that receives paint for channel @p c: the active layer when it is
     * an editable paint layer inside that channel's group, otherwise the
     * group's topmost editable paint layer.
     */
    KisNodeSP paintTarget(atherta::Channel c, KisNodeSP activeNode) const;

    /** Reads a channel (its group's projection) as straight-alpha RGBA8888. */
    QImage readChannel(atherta::Channel c, const QRect &rect) const;

    /** Adds the layer structure to the view's image as one undo step. */
    static bool create(KisViewManager *view);

private:
    KisImageSP m_image;
    KisNodeSP m_root;
    KisNodeSP m_channels[atherta::kChannelCount];
};
