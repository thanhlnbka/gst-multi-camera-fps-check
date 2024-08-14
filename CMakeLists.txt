cmake_minimum_required(VERSION 3.15)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

project(gst-multi-camera-fps-check)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-app-1.0)

add_executable(check_fps check_fps.cpp)

target_include_directories(check_fps PRIVATE ${GSTREAMER_INCLUDE_DIRS})
target_link_libraries(check_fps PRIVATE ${GSTREAMER_LIBRARIES} pthread)
