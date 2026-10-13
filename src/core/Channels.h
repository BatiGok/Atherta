/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstdint>

namespace atherta {

/**
 * PBR channels of a texture set. Each channel is a group layer in the Krita
 * document, so users can stack as many paint layers inside it as they like.
 */
enum class Channel : int {
    BaseColor = 0,
    Roughness,
    Metallic,
    Normal,
    Count
};

constexpr int kChannelCount = static_cast<int>(Channel::Count);

struct ChannelInfo {
    const char *id;          // stable identifier
    const char *layerName;   // name of the group layer in Krita
    uint8_t defaultRgba[4];  // value used where the channel is empty (sRGB 8 bit)
    bool isColor;            // true: colour data (sRGB); false: linear data
};

inline const ChannelInfo &channelInfo(Channel c)
{
    // Defaults match Blender's Principled BSDF where sensible:
    // roughness 0.5, metallic 0, flat tangent-space normal.
    static const ChannelInfo infos[kChannelCount] = {
        {"basecolor", "Base Color", {188, 188, 188, 255}, true},
        {"roughness", "Roughness", {128, 128, 128, 255}, false},
        {"metallic", "Metallic", {0, 0, 0, 255}, false},
        {"normal", "Normal", {128, 128, 255, 255}, false},
    };
    return infos[static_cast<int>(c)];
}

} // namespace atherta
