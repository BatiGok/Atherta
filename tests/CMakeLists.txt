# SPDX-FileCopyrightText: 2026 Atherta
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(atherta_core_tests
    test_main.cpp
    test_view_navigator.cpp
    test_mesh.cpp
    test_dilation.cpp
    test_stroke_region.cpp
)
target_include_directories(atherta_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/doctest)
target_link_libraries(atherta_core_tests PRIVATE atherta_core)

add_test(NAME atherta_core_tests COMMAND atherta_core_tests)
