cmake_minimum_required(VERSION 3.10)

# set the project name
project(varserver
    VERSION 0.1
    DESCRIPTION "Variable Server"
)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory(client)
add_subdirectory(server)
add_subdirectory(getvar)
add_subdirectory(setvar)
add_subdirectory(vars)
add_subdirectory(mkvar)
add_subdirectory(vartemplate)