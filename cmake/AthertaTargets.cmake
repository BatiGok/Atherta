# SPDX-FileCopyrightText: 2026 Atherta
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Defines the Atherta targets with absolute paths. Included by the
# standalone build (CMakeLists.txt) and, deferred, by Krita's build
# (cmake/KritaInject.cmake), where add_subdirectory() is not allowed.
#
# Requires ATHERTA_SOURCE_DIR.

set(_atherta_core ${ATHERTA_SOURCE_DIR}/src/core)
set(_atherta_plugin ${ATHERTA_SOURCE_DIR}/src/plugin)
set(_atherta_3rd ${ATHERTA_SOURCE_DIR}/third_party)

# Vendored C libraries, compiled without our warning flags.
add_library(atherta_thirdparty STATIC
    ${_atherta_3rd}/ufbx/ufbx.c
    ${_atherta_3rd}/mikktspace/mikktspace.c
)
target_include_directories(atherta_thirdparty SYSTEM PUBLIC
    ${_atherta_3rd}/ufbx
    ${_atherta_3rd}/mikktspace
)
if(MSVC)
    target_compile_options(atherta_thirdparty PRIVATE /w)
else()
    target_compile_options(atherta_thirdparty PRIVATE -w)
endif()
if(UNIX)
    target_link_libraries(atherta_thirdparty PUBLIC m)
endif()
# Krita compiles C as gnu90; ufbx needs C99.
set_target_properties(atherta_thirdparty PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC OFF
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)

# Qt-free core: navigation, mesh loading, BVH, baking helpers.
add_library(atherta_core STATIC
    ${_atherta_core}/ViewNavigator.cpp
    ${_atherta_core}/Mesh.cpp
    ${_atherta_core}/MeshBvh.cpp
    ${_atherta_core}/GutterDilation.cpp
    ${_atherta_core}/StrokeRegion.cpp
)
# Only src/ is put on the include path and headers are included as
# "core/Mesh.h": generic names like Mesh.h must never be found by a bare
# #include from system or library headers (Windows file names are
# case-insensitive, so a Math.h here would replace <math.h>).
target_include_directories(atherta_core PUBLIC ${ATHERTA_SOURCE_DIR}/src)
target_compile_features(atherta_core PUBLIC cxx_std_17)
target_link_libraries(atherta_core PRIVATE atherta_thirdparty)
# Linked into the Krita plugin, which is a shared module.
set_target_properties(atherta_core PROPERTIES POSITION_INDEPENDENT_CODE ON AUTOMOC OFF)

# The Krita docker plugin, built like Krita's own in-tree dockers.
if(TARGET kritaui)
    kis_add_library(kritaatherta3d MODULE
        ${_atherta_plugin}/Atherta3DPlugin.cpp
        ${_atherta_plugin}/Atherta3DDock.cpp
        ${_atherta_plugin}/TextureSet.cpp
        ${_atherta_plugin}/StrokeSession.cpp
        ${_atherta_plugin}/Renderer.cpp
        ${_atherta_plugin}/Viewport.cpp
    )
    set_target_properties(kritaatherta3d PROPERTIES AUTOMOC ON)
    target_link_libraries(kritaatherta3d kritaui atherta_core)
    install(TARGETS kritaatherta3d DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})
    message(STATUS "Atherta: building the kritaatherta3d plugin")

    # GL integration test (needs a display; see tests/gl/atherta_gl_test.cpp).
    if(ATHERTA_BUILD_GL_TEST)
        add_executable(atherta_gl_test
            ${ATHERTA_SOURCE_DIR}/tests/gl/atherta_gl_test.cpp
            ${_atherta_plugin}/Renderer.cpp
            ${_atherta_plugin}/Viewport.cpp
        )
        set_target_properties(atherta_gl_test PROPERTIES AUTOMOC ON)
        target_link_libraries(atherta_gl_test kritaui atherta_core)
    endif()
elseif(DEFINED KRITA_VERSION_STRING)
    message(WARNING "Atherta: kritaui target not found, the plugin will not be built")
endif()

unset(_atherta_core)
unset(_atherta_plugin)
unset(_atherta_3rd)
