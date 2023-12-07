# We can piggyback off of the parent project in this case
add_executable(${PROJECT_NAME}_test 
    varobject_tests.cpp
)

# Add sanitizers to compile and link options
target_compile_options(${PROJECT_NAME}_test
    PRIVATE 
    -fsanitize=address 
    -fsanitize=undefined
)

target_link_options(${PROJECT_NAME}_test
    PRIVATE 
    -fsanitize=address 
    -fsanitize=undefined
)

target_link_libraries(${PROJECT_NAME}_test
    PRIVATE 
    varserver
    # Currently, it is simpler for us to let Catch2 define main for us
    Catch2::Catch2WithMain
)

# This requires include(Catch) and allows for automatic test registration
catch_discover_tests(${PROJECT_NAME}_test)
