#pragma once

// Unified DLL export/import macro for camera_wrapper library.
// When building the DLL itself, define CAMERA_WRAPPER_BUILDING_DLL.
// Consumers of the DLL should NOT define that macro; the import decoration
// is applied automatically.

#if defined(_WIN32) || defined(_WIN64)
#if defined(CAMERA_WRAPPER_BUILDING_DLL)
#define CAMERA_WRAPPER_API __declspec(dllexport)
#else
#define CAMERA_WRAPPER_API __declspec(dllimport)
#endif
#else
// GCC / Clang: use visibility attribute.
// CMake sets -fvisibility=hidden globally; only symbols marked here are exported.
#define CAMERA_WRAPPER_API __attribute__((visibility("default")))
#endif
