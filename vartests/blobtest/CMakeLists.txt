cmake_minimum_required(VERSION 3.10)

include(GNUInstallDirs)

project(blobtest
	VERSION ${VARSERVER_VERSION}
	DESCRIPTION "Variable Server Blob Test"
)

add_executable( ${PROJECT_NAME}
	src/blobtest.c
)

target_include_directories( ${PROJECT_NAME}
	PRIVATE inc
    PRIVATE ../../../client/inc
)

target_compile_options( ${PROJECT_NAME}
	PRIVATE
	-Wall
	-Wextra
	-Wpedantic
	-Werror
)

target_link_libraries( ${PROJECT_NAME}
	${CMAKE_THREAD_LIBS_INIT}
	rt
	varserver
)

install(TARGETS ${PROJECT_NAME}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
