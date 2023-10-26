cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_definitions(WITHOUT_COREDSP)

include_directories(lib/v06x/src)
project(paneltest)


