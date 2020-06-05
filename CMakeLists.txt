cmake_minimum_required(VERSION 3.10)
project(cragsman CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/externals/cage/cmake")
include(cage_all)
cage_default_init()
cage_directory_link("${CMAKE_CURRENT_SOURCE_DIR}/data/cragsman" "${CMAKE_BINARY_DIR}/result/data/cragsman")

add_subdirectory(externals/cage)

file(GLOB_RECURSE cragsman-sources "sources/*")
add_executable(cragsman ${cragsman-sources})
target_link_libraries(cragsman cage-engine)
cage_ide_category(cragsman cragsman)
cage_ide_sort_files(cragsman)
cage_ide_working_dir_in_place(cragsman)
