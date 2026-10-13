# SPDX-FileCopyrightText: 2026 Atherta
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(atherta_core STATIC
    ViewNavigator.cpp
)

target_include_directories(atherta_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(atherta_core PUBLIC cxx_std_17)
# Linked into the Krita plugin, which is a shared module.
set_target_properties(atherta_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
