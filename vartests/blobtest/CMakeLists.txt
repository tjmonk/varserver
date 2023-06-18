cmake_minimum_required(VERSION 3.10)

include(GNUInstallDirs)

project(blobtest
	VERSION 0.1
	DESCRIPTION "Variable Server Blob Test"
)

find_library( LIB_VARSERVER varserver REQUIRED )

add_executable( ${PROJECT_NAME}
	src/blobtest.c
)

target_include_directories( ${PROJECT_NAME}
	PRIVATE inc
    PRIVATE ../../../client/inc
)

target_link_libraries( ${PROJECT_NAME}
	${CMAKE_THREAD_LIBS_INIT}
	rt
	${LIB_VARSERVER}
)

install(TARGETS ${PROJECT_NAME}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
