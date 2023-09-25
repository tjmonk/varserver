cmake_minimum_required(VERSION 3.10)

# set the project name
project(varserver
	VERSION ${VARSERVER_VERSION}
	DESCRIPTION "Variable Server Interface Functions"
)

include(GNUInstallDirs)

#add the library
add_library( ${PROJECT_NAME} SHARED
 	src/varobject.c
	src/varprint.c
	src/varserver.c
    src/varcache.c
    src/varfp.c
    src/varquery.c
    src/vartemplate.c
)

set_target_properties( ${PROJECT_NAME} PROPERTIES
	VERSION ${PROJECT_VERSION}
	SOVERSION 1
)

target_link_libraries( ${PROJECT_NAME} rt )

set(VARSERVER_HEADERS
    inc/varserver/varclient.h
    inc/varserver/varobject.h
    inc/varserver/var.h
    inc/varserver/varserver.h
    inc/varserver/varprint.h
    inc/varserver/varcache.h
    inc/varserver/varfp.h
    inc/varserver/varquery.h
    inc/varserver/vartemplate.h)

set_target_properties(${PROJECT_NAME}
    PROPERTIES
    PUBLIC_HEADER "${VARSERVER_HEADERS}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_include_directories( ${PROJECT_NAME} PRIVATE . )

target_include_directories( ${PROJECT_NAME} PUBLIC inc )

target_compile_options( ${PROJECT_NAME}
	PUBLIC
	-Wall
	-Wextra
	-Wpedantic
	-Werror
)

install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/varserver)
