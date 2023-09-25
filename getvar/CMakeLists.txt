cmake_minimum_required(VERSION 3.10)

include(GNUInstallDirs)

project(getvar
	VERSION ${VARSERVER_VERSION}
	DESCRIPTION "Utility to get a Variable value"
)

add_executable( ${PROJECT_NAME}
	src/getvar.c
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
	PUBLIC
	-Wall
	-Wextra
	-Wpedantic
	-Werror
)

set_target_properties(${PROJECT_NAME}
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS ${PROJECT_NAME}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
