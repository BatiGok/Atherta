# SPDX-FileCopyrightText: 2026 Atherta
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Atherta 3D Paint for Krita.
#
# This directory builds in two ways:
#
#  1. Standalone (`cmake -S . -B build`): only the Qt-free core and its unit
#     tests. Useful for fast iteration without a Krita build.
#
#  2. Inside Krita's own build, injected with
#     -DCMAKE_PROJECT_krita_INCLUDE=<this repo>/cmake/KritaInject.cmake
#     Krita's sources are not modified; the plugin is built against Krita's
#     internal libraries exactly like an in-tree plugin.

cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED KRITA_VERSION_STRING)
    project(Atherta3D LANGUAGES C CXX)
    set(ATHERTA_STANDALONE ON)
    include(CTest)
else()
    set(ATHERTA_STANDALONE OFF)
endif()

option(ATHERTA_BUILD_TESTS "Build the Atherta core unit tests" ${ATHERTA_STANDALONE})

add_subdirectory(src/core)

if(ATHERTA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(TARGET kritaui)
    add_subdirectory(src/plugin)
elseif(NOT ATHERTA_STANDALONE)
    message(WARNING "Atherta: kritaui target not found, the plugin will not be built")
endif()
