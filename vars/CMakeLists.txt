cmake_minimum_required(VERSION 3.10)

project(vars
	VERSION ${VARSERVER_VERSION}
    DESCRIPTION "Variable Query Utility"
)

include(GNUInstallDirs)

add_executable( ${PROJECT_NAME}
    src/vars.c
)

target_link_libraries( ${PROJECT_NAME}
	varserver
)

target_include_directories( ${PROJECT_NAME} PRIVATE
	.
	inc
    ../client/inc
	${CMAKE_BINARY_DIR} )

target_compile_options( ${PROJECT_NAME}
	PRIVATE
	-Wall
	-Wextra
	-Wpedantic
	-Werror
)

install(TARGETS ${PROJECT_NAME}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} )

