# SPDX-FileCopyrightText: 2026 Atherta
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Adds the Atherta plugin to a Krita build without modifying Krita's sources.
#
#   cmake <krita-source> -DCMAKE_PROJECT_krita_INCLUDE=<atherta>/cmake/KritaInject.cmake ...
#
# CMake includes this file right after Krita's project() call. The targets
# are defined at the end of Krita's top-level CMakeLists.txt (deferred), so
# that all of Krita's library targets (kritaui, kritaimage, ...) exist.

if(CMAKE_VERSION VERSION_LESS 3.19)
    message(FATAL_ERROR "Atherta needs CMake 3.19 or newer (cmake_language(DEFER)).")
endif()

get_filename_component(ATHERTA_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
message(STATUS "Atherta: plugin sources at ${ATHERTA_SOURCE_DIR}")

# Bake the path into the deferred call; add_subdirectory() is not allowed
# during deferred execution, so the targets are defined by include().
cmake_language(EVAL CODE "
    cmake_language(DEFER DIRECTORY [[${CMAKE_SOURCE_DIR}]]
                   CALL include [[${ATHERTA_SOURCE_DIR}/cmake/AthertaTargets.cmake]])
")
