cmake_minimum_required(VERSION 3.10)

project(MediaLib)

add_library(mlib STATIC src/png.c src/jpeg.c src/utils.c)
set_target_properties(mlib PROPERTIES PREFIX "")
target_link_libraries(mlib ${CMAKE_SOURCE_DIR}/zlib/libz.a)

add_library(mlibp STATIC src/image.cpp)
set_target_properties(mlibp PROPERTIES PREFIX "")
target_link_libraries(mlibp mlib)

add_executable(mmain src/main.c)
target_link_libraries(mmain mlib)

add_executable(mmainp src/main.cpp)
target_link_libraries(mmainp mlibp)
