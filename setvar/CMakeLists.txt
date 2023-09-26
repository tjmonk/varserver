cmake_minimum_required(VERSION 3.10)

include(GNUInstallDirs)

project(setvar
	VERSION ${VARSERVER_VERSION}
	DESCRIPTION "Utility to set a Variable value"
)

add_executable( ${PROJECT_NAME}
	src/setvar.c
)

target_include_directories( ${PROJECT_NAME}
	PRIVATE inc
    PRIVATE ../client/inc
)

target_link_libraries( ${PROJECT_NAME}
	${CMAKE_THREAD_LIBS_INIT}
	rt
	varserver
)

target_compile_options( ${PROJECT_NAME}
	PRIVATE
	-Wall
	-Wextra
	-Wpedantic
	-Werror
)

install(TARGETS ${PROJECT_NAME}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
