cmake_minimum_required(VERSION 3.10)

set(VARSERVER_VERSION 0.1)

# set the project name
project(varserver
    VERSION ${VARSERVER_VERSION}
    DESCRIPTION "Variable Server"
)

# Pull in Catch2 from the web
include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_SHALLOW    TRUE
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.4.0)
FetchContent_MakeAvailable(Catch2)

# For catch v3 or later, make Catch.cmake and its macros available
list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)

include(CTest)
include(Catch)

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
add_subdirectory(varflags)
add_subdirectory(vartests)