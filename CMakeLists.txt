cmake_minimum_required(VERSION 3.17)

project(camera_wrapper_root VERSION 1.0.0 LANGUAGES CXX)

# ── Demo build defaults ──────────────────────────────────────────────
# Standalone build  → demos ON by default.
# Used as a subdirectory (e.g. via add_subdirectory / FetchContent)
# → demos OFF by default.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_demos_default ON)
else()
    set(_demos_default OFF)
endif()

option(BUILD_DEMOS "Build all demo projects" ${_demos_default})
option(BUILD_CPP_DEMO "Build C++ console demo" ${_demos_default})
option(BUILD_QT_DEMO "Build Qt GUI demo" ${_demos_default})

add_subdirectory(wrapper)

if(BUILD_DEMOS)
    if(BUILD_CPP_DEMO)
        add_subdirectory(demos/cpp_demo)
    endif()

    if(BUILD_QT_DEMO)
        add_subdirectory(demos/qt_demo)
    endif()
endif()