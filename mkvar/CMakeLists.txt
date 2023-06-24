cmake_minimum_required(VERSION 3.10)

include(GNUInstallDirs)

project(mkvar
	VERSION ${VARSERVER_VERSION}
	DESCRIPTION "Utility to create a Variable"
)

add_executable( ${PROJECT_NAME}
	src/mkvar.c
)

target_include_directories( ${PROJECT_NAME}
	PRIVATE inc
    PRIVATE ../client/include
)

target_link_libraries( ${PROJECT_NAME}
	${CMAKE_THREAD_LIBS_INIT}
	rt
	varserver
)

install(TARGETS ${PROJECT_NAME}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
