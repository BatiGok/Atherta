# SPDX-FileCopyrightText: 2026 Atherta
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Atherta 3D Paint for Krita.
#
# Standalone (`cmake -S . -B build`) this builds the Qt-free core and its
# unit tests, which is useful for fast iteration without a Krita build.
#
# The Krita plugin itself is built inside Krita's build tree, injected with
#   -DCMAKE_PROJECT_krita_INCLUDE=<this repo>/cmake/KritaInject.cmake
# without modifying Krita's sources (see README.md).

cmake_minimum_required(VERSION 3.16)
project(Atherta3D LANGUAGES C CXX)

set(ATHERTA_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
include(cmake/AthertaTargets.cmake)

option(ATHERTA_BUILD_TESTS "Build the Atherta core unit tests" ON)
if(ATHERTA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
