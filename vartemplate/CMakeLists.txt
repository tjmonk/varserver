cmake_minimum_required(VERSION 3.10)

include(GNUInstallDirs)

project(vartemplate
	VERSION ${VARSERVER_VERSION}
	DESCRIPTION "Template Rendering Utility"
)

add_executable( ${PROJECT_NAME}
	src/vartemplate.c
)

target_include_directories( ${PROJECT_NAME}
	PRIVATE inc
)

target_link_libraries( ${PROJECT_NAME}
	varserver
	rt
)

target_compile_options( ${PROJECT_NAME}
	PUBLIC
	-Wall
	-Wextra
	-Wpedantic
	-Werror
)

install(TARGETS ${PROJECT_NAME}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
