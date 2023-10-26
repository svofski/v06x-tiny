# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources "${CMAKE_SOURCE_DIR}/main/*.*" "${CMAKE_SOURCE_DIR}/lib/*.*")

message("app_sources: ${app_sources}  CMAKE_SOURCE_DIR: ${CMAKE_SOURCE_DIR}")

idf_component_register(SRCS ${app_sources})

component_compile_options("-funroll-loops") # "-fjump-tables" "-ftree-switch-conversion") #  "-mfix-esp32-psram-cache-issue" "-mfix-esp32-psram-cache-strategy=memw")  #-fjump-tables -ftree-switch-conversion
#component_compile_options("-funroll-loops"  "-mfix-esp32-psram-cache-issue" "-mfix-esp32-psram-cache-strategy=memw") # "-fjump-tables" "-ftree-switch-conversion") # )  #-fjump-tables -ftree-switch-conversion
#component_compile_options("-funroll-loops"  "-mfix-esp32-psram-cache-strategy=memw") # "-fjump-tables" "-ftree-switch-conversion") # )  #-fjump-tables -ftree-switch-conversion
