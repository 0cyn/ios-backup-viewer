cmake_minimum_required(VERSION 4.2)
project(iosbackups LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(src/lib)

set(QHEXVIEW_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/QHexView)

add_subdirectory(src/ui)
add_subdirectory(src/cli)
