cmake_minimum_required(VERSION 3.17)

project(camera_wrapper VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Dependencies ────────────────────────────────────────────────────
find_package(OpenCV REQUIRED)

# Hikrobot MVS SDK – assumed installed; MVCAM_COMMON_RUNENV points to the
# SDK root (set by the MVS installer).  Users can override with
# -DHIKCAM_SDK_DIR=<path>.
if(NOT DEFINED HIKCAM_SDK_DIR)
    if(DEFINED ENV{MVCAM_COMMON_RUNENV})
        set(HIKCAM_SDK_DIR "$ENV{MVCAM_COMMON_RUNENV}")
    else()
        message(FATAL_ERROR
            "Hikrobot MVS SDK not found.\n"
            "Install the MVS SDK (sets MVCAM_COMMON_RUNENV) or pass "
            "-DHIKCAM_SDK_DIR=<path> to cmake.")
    endif()
endif()

if(WIN32)
    set(_hik_lib_subdir "Libraries/win64")
else()
    set(_hik_lib_subdir "lib/aarch64") # adjust for your Linux arch
endif()

find_library(HIKCAMERA_LIBRARY
    NAMES MvCameraControl
    PATHS "${HIKCAM_SDK_DIR}/${_hik_lib_subdir}"
    NO_DEFAULT_PATH
    REQUIRED
)

# ── Library target ───────────────────────────────────────────────────
add_library(camera_wrapper SHARED
    include/camera_wrapper/export.h
    include/camera_wrapper/device_info.h
    include/camera_wrapper/frame_callback_manager.h
    include/camera_wrapper/frame_queue.h
    include/camera_wrapper/grab_config.h
    include/camera_wrapper/camera.h
    include/camera_wrapper/camera_enumerator.h
    include/camera_wrapper/enumerator_factory.h
    include/camera_wrapper/image_frame.h
    include/camera_wrapper/status_callback_manager.h
    include/camera_wrapper/transport_config.h
    src/device_info.cpp
    src/frame_callback_manager.cpp
    src/frame_queue.cpp
    src/status_callback_manager.cpp
    src/enumerator_factory.cpp
    src/hik_camera.h
    src/hik_camera.cpp
    src/hik_enumerator.h
    src/hik_enumerator.cpp
)

target_include_directories(camera_wrapper
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    "${HIKCAM_SDK_DIR}/Includes"
)

target_compile_definitions(camera_wrapper PRIVATE CAMERA_WRAPPER_BUILDING_DLL)

# Suppress MSVC C4819: source file contains characters that cannot be
# represented in the current code page.  All source files are UTF-8.
if(MSVC)
    target_compile_options(camera_wrapper PRIVATE /utf-8)
endif()

target_link_libraries(camera_wrapper
    PUBLIC ${OpenCV_LIBS}
    PRIVATE ${HIKCAMERA_LIBRARY}
)

# Create an ALIAS target for namespaced access
add_library(camera_wrapper::camera_wrapper ALIAS camera_wrapper)

# Linux: export only symbols marked CAMERA_WRAPPER_API.
if(NOT WIN32)
    set_target_properties(camera_wrapper PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
endif()

# ── Install rules ────────────────────────────────────────────────────
install(TARGETS camera_wrapper
	EXPORT camera_wrapper_targets
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib)

install(DIRECTORY include/camera_wrapper DESTINATION include)

install(EXPORT camera_wrapper_targets
	FILE camera_wrapper_targets.cmake
	NAMESPACE camera_wrapper::
	DESTINATION lib/cmake/camera_wrapper)
