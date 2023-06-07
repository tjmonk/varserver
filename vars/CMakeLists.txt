cmake_minimum_required(VERSION 3.10)

project(vars
	VERSION 0.1
    DESCRIPTION "Variable Query Utility"
)

include(GNUInstallDirs)

find_library( LIB_VARSERVER varserver REQUIRED )

add_executable( ${PROJECT_NAME}
    src/vars.c
)

target_link_libraries( ${PROJECT_NAME}
	${LIB_VARSERVER}
)

target_include_directories( ${PROJECT_NAME} PRIVATE
	.
	inc
    ../client/inc
	${CMAKE_BINARY_DIR} )

install(TARGETS ${PROJECT_NAME}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} )

