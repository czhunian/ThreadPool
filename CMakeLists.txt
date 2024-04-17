cmake_minimum_required(VERSION 3.0)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -g)

aux_source_directory(. SRC_LIST)

add_executable(main ${SRC_LIST})

target_link_libraries(main pthread)