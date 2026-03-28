cmake_minimum_required(VERSION 3.17)

project(qt_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT TARGET CameraWrapper)
    find_package(CameraWrapper REQUIRED)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Concurrent)
find_package(OpenCV REQUIRED)

set(SOURCES
    main.cpp
    main_window.h
    main_window.cpp
    main_window.ui
    camera_bridge.h
    camera_bridge.cpp
)

qt_add_executable(qt_demo MANUAL_FINALIZATION ${SOURCES})

target_include_directories(qt_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../wrapper/src
)

target_link_libraries(qt_demo PRIVATE
    CameraWrapper
    Qt6::Core
    Qt6::Widgets
    ${OpenCV_LIBS}
)

if(MSVC)
    target_compile_options(qt_demo PRIVATE /utf-8)
endif()

qt_finalize_executable(qt_demo)

if(WIN32)
    add_custom_command(TARGET qt_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:CameraWrapper>
        $<TARGET_FILE_DIR:qt_demo>
        COMMENT "Copying CameraWrapper.dll")
endif()
