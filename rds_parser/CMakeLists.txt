# Add the source files for the rds_parser library
aux_source_directory(. DIR_rds_parser_SRCS)

# Create the rds_parser library
add_library(rds_parser ${DIR_rds_parser_SRCS})

# Add the include directory for this library
target_include_directories(rds_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Link any required libraries (if needed)
target_link_libraries(rds_parser PUBLIC pico_stdlib)