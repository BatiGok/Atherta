# SPDX-FileCopyrightText: 2026 Atherta
# SPDX-License-Identifier: GPL-3.0-or-later

set(ATHERTA_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party)

# Vendored C libraries, compiled without our warning flags.
add_library(atherta_thirdparty STATIC
    ${ATHERTA_THIRD_PARTY}/ufbx/ufbx.c
    ${ATHERTA_THIRD_PARTY}/mikktspace/mikktspace.c
)
target_include_directories(atherta_thirdparty SYSTEM PUBLIC
    ${ATHERTA_THIRD_PARTY}/ufbx
    ${ATHERTA_THIRD_PARTY}/mikktspace
)
if(MSVC)
    target_compile_options(atherta_thirdparty PRIVATE /w)
else()
    target_compile_options(atherta_thirdparty PRIVATE -w)
endif()
if(UNIX)
    target_link_libraries(atherta_thirdparty PUBLIC m)
endif()
set_target_properties(atherta_thirdparty PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(atherta_core STATIC
    ViewNavigator.cpp
    Mesh.cpp
    MeshBvh.cpp
)

target_include_directories(atherta_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(atherta_core PUBLIC cxx_std_17)
target_link_libraries(atherta_core PRIVATE atherta_thirdparty)
# Linked into the Krita plugin, which is a shared module.
set_target_properties(atherta_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
