cmake_minimum_required(VERSION 3.17)

project(cpp_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT TARGET camera_wrapper)
	find_package(camera_wrapper REQUIRED)
endif()

find_package(OpenCV REQUIRED)

add_executable(cpp_demo main.cpp)

if(MSVC)
    target_compile_options(cpp_demo PRIVATE /utf-8)
endif()

target_link_libraries(cpp_demo PRIVATE camera_wrapper::camera_wrapper ${OpenCV_LIBS})

if(WIN32)
    add_custom_command(TARGET cpp_demo POST_BUILD
    	COMMAND ${CMAKE_COMMAND} -E copy_if_different
    	$<TARGET_FILE:camera_wrapper>
    	$<TARGET_FILE_DIR:cpp_demo>
    	COMMENT "Copying camera_wrapper.dll")
endif()
