cmake_minimum_required(VERSION 3.10)

include(GNUInstallDirs)

project(vartemplate
	VERSION 0.1
	DESCRIPTION "Template Rendering Utility"
)

find_library( LIB_VARSERVER varserver REQUIRED )

add_executable( ${PROJECT_NAME}
	src/vartemplate.c
)

target_include_directories( ${PROJECT_NAME}
	PRIVATE inc
)

target_link_libraries( ${PROJECT_NAME}
	${LIB_VARSERVER}
	rt
)

install(TARGETS ${PROJECT_NAME}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
