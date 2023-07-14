cmake_minimum_required(VERSION 3.10)

include(GNUInstallDirs)

project(varserver-bin
    VERSION ${VARSERVER_VERSION}
    DESCRIPTION "Variable Server"
)

#SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")

set(CMAKE_BUILD_TYPE Debug)

find_package( Threads REQUIRED )

add_executable( ${PROJECT_NAME}
    src/blocklist.c
    src/notify.c
    src/server.c
    src/taglist.c
    src/transaction.c
    src/validate.c
    src/varlist.c
    src/stats.c
)

target_include_directories( ${PROJECT_NAME}
    PRIVATE ../client/inc
    PRIVATE inc
    PRIVATE .
)

target_link_libraries( ${PROJECT_NAME}
    ${CMAKE_THREAD_LIBS_INIT}
    rt
    varserver
)

set_target_properties( ${PROJECT_NAME}
    PROPERTIES
    OUTPUT_NAME varserver
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)