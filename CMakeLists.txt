cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_definitions(WITHOUT_COREDSP)
#add_compile_options(-mfix-esp32-psram-cache-issue -mfix-esp32-psram-cache-strategy=memw)


include_directories(lib/v06x/src lib/ESP32Lib/src main)
#list(APPEND EXTRA_COMPONENT_DIRS components/pwm_audio)
project(paneltest)


