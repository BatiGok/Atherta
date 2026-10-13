# SPDX-FileCopyrightText: 2026 Atherta
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Adds the Atherta plugin to a Krita build without modifying Krita's sources.
#
#   cmake <krita-source> -DCMAKE_PROJECT_krita_INCLUDE=<atherta>/cmake/KritaInject.cmake ...
#
# CMake includes this file right after Krita's project() call. The actual
# add_subdirectory() is deferred to the end of Krita's top-level CMakeLists.txt
# so that all of Krita's library targets (kritaui, kritaimage, ...) exist.

if(CMAKE_VERSION VERSION_LESS 3.19)
    message(FATAL_ERROR "Atherta needs CMake 3.19 or newer (cmake_language(DEFER)).")
endif()

get_filename_component(_atherta_source_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_atherta_binary_dir "${CMAKE_BINARY_DIR}/atherta3d")

message(STATUS "Atherta: plugin sources at ${_atherta_source_dir}")

# Bake the paths into the deferred call so it does not depend on variables
# that might change while Krita's CMakeLists.txt is processed.
cmake_language(EVAL CODE "
    cmake_language(DEFER DIRECTORY [[${CMAKE_SOURCE_DIR}]]
                   CALL add_subdirectory [[${_atherta_source_dir}]] [[${_atherta_binary_dir}]])
")

unset(_atherta_source_dir)
unset(_atherta_binary_dir)
