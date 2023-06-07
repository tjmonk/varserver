cmake_minimum_required(VERSION 3.10)

# set the project name
project(varserver
	VERSION 0.1
	DESCRIPTION "Variable Server"
)

add_subdirectory(client)
add_subdirectory(server)
add_subdirectory(getvar)
add_subdirectory(setvar)
add_subdirectory(vars)
add_subdirectory(mkvar)
add_subdirectory(vartemplate)