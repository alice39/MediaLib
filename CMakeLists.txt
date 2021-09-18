cmake_minimum_required(VERSION 3.10)

project(MediaLib)

add_library(mlib STATIC png.c jpeg.c utils.c)
set_target_properties(mlib PROPERTIES PREFIX "")
target_link_libraries(mlib ${CMAKE_SOURCE_DIR}/zlib/libz.a)

add_executable(mmain main.c)
target_link_libraries(mmain mlib)